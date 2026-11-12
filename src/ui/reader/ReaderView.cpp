#include "ui/reader/ReaderView.hpp"

#include "ui/reader/StripCanvas.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <utility>

namespace snx::ui {

namespace {

constexpr float kSwipeMinDistance = 80;  // a shorter sideways move is a tap or a nudge, not a page turn
// A touch that slid at most this far and let go this quickly is a tap, even when borealis took it for a drag.
constexpr float kTapMaxSlide = 40;
constexpr int kTapMaxHeldMs = 300;
// WP-13: a second tap this soon after the first (and close to it, core::isDoubleTap) makes a double tap; a single middle
// tap waits this long before it opens the menu.
constexpr int kDoubleTapGapMs = 250;
constexpr float kDoubleTapZoom = 2;
constexpr float kSpinnerRadius = 24;
constexpr double kPi = 3.14159265358979323846;

core::SizeF scaled(core::SizeF size, float zoom) {
    return {size.width * zoom, size.height * zoom};
}

}  // namespace

// Sees every finger's touch: borealis hands each finger to the recognizers of the view it touched, one call per finger with
// its own id (application.cpp's touch loop), while its pan recognizer follows only one finger. It passes them to the view
// and always answers FAILED, so it never interrupts the tap and pan recognizers and they never interrupt it.
class ReaderView::PinchRecognizer : public brls::GestureRecognizer {
  public:
    explicit PinchRecognizer(ReaderView* view) : view_(view) {}

    brls::GestureState recognitionLoop(brls::TouchState touch, brls::MouseState mouse, brls::View*,
                                       brls::Sound*) override {
        if (touch.phase == brls::TouchPhase::NONE) {
            // The desktop's mouse: no touch, the left button's phase instead (as borealis's pan reads it), as finger 0.
            touch.fingerId = 0;
            touch.position = mouse.position;
            touch.phase = mouse.leftButton;
        }
        if (touch.phase != brls::TouchPhase::NONE) {
            view_->onFinger(touch);
        }
        return brls::GestureState::FAILED;
    }

  private:
    ReaderView* view_;
};

ReaderView::ReaderView(image::TextureBudget& textures) : budget_(textures) {
    setFocusable(true);
    setHideHighlight(true);
    overlay_ = new brls::Box(brls::Axis::COLUMN);
    overlay_->setPositionType(brls::PositionType::ABSOLUTE);
    overlay_->setPositionTop(0);
    overlay_->setPositionLeft(0);
    overlay_->setJustifyContent(brls::JustifyContent::CENTER);
    overlay_->setAlignItems(brls::AlignItems::CENTER);
    addView(overlay_);
    // First, so the fingers are counted before the tap and pan recognizers see the same touch.
    addGestureRecognizer(new PinchRecognizer(this));
    addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound*) {
        if (status.state == brls::GestureState::END) {
            onTapped(status.position);
        }
    }));
    addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound*) { onPan(status); }, brls::PanAxis::ANY));
}

std::int64_t ReaderView::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void ReaderView::gesture(const char* kind, bool started) {
    if (onGesture) {
        onGesture(kind, started);
    }
}

void ReaderView::setOrientation(core::ReaderOrientation orientation, core::SizeF screen) {
    orientation_ = orientation;
    real_ = screen;
    const core::SizeF reader = readerScreen();
    overlay_->setWidth(reader.width);
    overlay_->setHeight(reader.height);
    placed_ = false;
    setZoomValue(1);  // rotating goes back to the fit
    glide_.reset();
    pinch_.reset();
    pendingTap_.reset();
}

void ReaderView::addButton(brls::View* button, std::function<void()> action) {
    buttons_.push_back({button, std::move(action)});
}

brls::View* ReaderView::hitTest(brls::Point point) {
    // Portrait: no borealis tap may land on a child at its unturned place, so this view takes them all.
    return core::isPortrait(orientation_) ? brls::View::hitTest(point) : brls::Box::hitTest(point);
}

void ReaderView::getCullingBounds(float* top, float* right, float* bottom, float* left) {
    const core::SizeF screen = readerScreen();
    *top = getY();
    *left = getX();
    *right = *left + screen.width;
    *bottom = *top + screen.height;
}

core::PointF ReaderView::toReader(brls::Point position) {
    return core::screenToReader(orientation_, {position.x - getX(), position.y - getY()}, {getWidth(), getHeight()});
}

bool ReaderView::shown(brls::View* view) {
    for (brls::View* v = view; v != nullptr && v != this; v = v->getParent()) {
        if (v->getVisibility() != brls::Visibility::VISIBLE) {
            return false;
        }
    }
    return true;
}

void ReaderView::setStrip(StripCanvas* strip) {
    strip_ = strip;
    freeTextures();
    image_.reset();
    pageKey_.clear();
    half_.reset();
    placed_ = false;
    refused_ = false;
    setZoomValue(1);
    glide_.reset();
    pinch_.reset();
    pendingTap_.reset();
    swallowTap_ = false;
}

void ReaderView::setDoubleTapWait(bool on) {
    doubleTapWait_ = on;
    if (!on) {
        pendingTap_.reset();
    }
}

void ReaderView::onTapped(brls::Point position) {
    if (multiTouch_) {
        return;  // a finger lifting after a pinch is no tap
    }
    if (swallowTap_) {
        swallowTap_ = false;  // this touch stopped a flick
        return;
    }
    const core::PointF at = toReader(position);
    // In portrait every tap comes here. In landscape borealis runs a tapped button itself, but a tap that slid a little
    // (onPan) comes here. The overlay's children are laid out in reader units from this view's corner.
    const brls::Point local{getX() + at.x, getY() + at.y};
    for (const Button& button : buttons_) {
        if (shown(button.view) && button.view->getFrame().pointInside(local)) {
            pendingTap_.reset();
            brls::sync(button.action);  // after the tap is done (the action may close this screen)
            return;
        }
    }
    if (strip_) {
        if (onStripTap) {
            onStripTap(core::stripTapArea(at.y, readerScreen().height));  // at once: no double tap in the strip (WP-13d)
        }
        return;
    }
    const core::TapArea area = core::tapArea(at.x, readerScreen().width);
    if (area == core::TapArea::Middle && doubleTapWait_) {
        onMiddleTap(at);
        return;
    }
    if (pendingTap_) {
        firePendingTap();
    }
    if (onTap) {
        onTap(area);  // the edges turn at once
    }
}

void ReaderView::onMiddleTap(core::PointF at) {
    if (!pendingTap_ || !pendingTap_->secondDown) {
        pendingTap_ = PendingTap{at, nowMs(), false};  // the menu waits: a second tap may follow
        return;
    }
    // The second tap of a double tap: 2 x at this point, or back to the whole page.
    pendingTap_.reset();
    if (!placed_ || !hasPage()) {
        return;
    }
    gesture("double_tap", true);
    if (core::isZoomed(zoom_)) {
        resetZoom();
    } else {
        zoomTo(std::min(kDoubleTapZoom, maxZoom_), at);
    }
    gesture("double_tap", false);
}

void ReaderView::firePendingTap() {
    pendingTap_.reset();
    if (onTap) {
        onTap(core::TapArea::Middle);
    }
}

ReaderView::~ReaderView() {
    freeTextures();
}

void ReaderView::freeTextures() {
    NVGcontext* vg = brls::Application::getNVGContext();
    for (const int texture : textures_) {
        if (texture != 0 && vg != nullptr) {
            nvgDeleteImage(vg, texture);
        }
    }
    textures_.clear();
    leases_.clear();
}

bool ReaderView::makeTextures(const image::ImagePtr& image, std::vector<int>& textures,
                              std::vector<image::TextureLease>& leases) {
    // One texture per tile (each side <= maxTextureSide), each one reserved in the GPU budget first (S7).
    NVGcontext* vg = brls::Application::getNVGContext();
    std::vector<std::uint8_t> scratch;
    const auto& tiles = image->tiles();
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        image::TextureLease lease(budget_, image::TextureBudget::bytesFor(tiles[i]));
        const std::uint8_t* pixels = lease.ok() ? image->tilePixels(i, scratch) : nullptr;
        const int texture = pixels ? nvgCreateImageRGBA(vg, tiles[i].width, tiles[i].height, 0, pixels) : 0;
        if (texture == 0) {
            for (const int made : textures) {
                nvgDeleteImage(vg, made);
            }
            textures.clear();
            leases.clear();
            return false;
        }
        textures.push_back(texture);
        leases.push_back(std::move(lease));
    }
    return true;
}

void ReaderView::showPage(const image::ImagePtr& image, core::PageFit fit, core::ReadingDirection direction,
                          const std::string& pageKey, std::optional<core::Side> half) {
    if (image == image_ && fit == fit_ && direction == direction_ && pageKey == pageKey_ && half == half_) {
        return;
    }
    const bool samePage = pageKey == pageKey_ && fit == fit_ && direction == direction_ && half == half_;
    if (samePage && image && image_ && image->sourceSize() == image_->sourceSize()) {
        // A sharper decode of the page on screen (or its fit image again): the new textures first, then the old ones go
        // (S7: both are reserved for that moment). Zoom and position stay. No room: the old image stays, only blurrier.
        std::vector<int> textures;
        std::vector<image::TextureLease> leases;
        const auto started = std::chrono::steady_clock::now();
        if (!makeTextures(image, textures, leases)) {
            return;
        }
        uploadMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
        freeTextures();
        textures_ = std::move(textures);
        leases_ = std::move(leases);
        image_ = image;
        ++imageRevision_;
        return;
    }
    const bool newPage = pageKey != pageKey_;  // the key names the half too: the other half of a spread is a new page
    const bool fitChanged = fit != fit_;
    pageKey_ = pageKey;
    fit_ = fit;
    direction_ = direction;
    half_ = half;
    placed_ = false;
    glide_.reset();
    pinch_.reset();  // fingers still down count for nothing until they lift (multiTouch_)
    if (fitChanged || (newPage && !keepZoomOnTurn_)) {
        setZoomValue(1);  // a new fit, or a turn with "Back to whole page"
    } else if (newPage && core::isZoomed(zoom_)) {
        zoomChanged();  // "Keep the zoom": the new page decodes again for it
    }
    if (image == image_) {
        if (newPage) {
            ++revision_;  // the other half of the same image: its textures stay
        }
        return;
    }
    freeTextures();
    image_ = image;
    refused_ = false;
    drawn_ = {};
    ++revision_;
    if (image_ && !makeTextures(image_, textures_, leases_)) {
        freeTextures();
        refused_ = true;
    }
}

void ReaderView::layout(core::SizeF screen) {
    screen_ = screen;
    // A half of a split spread fits like a page of its own (WP-13c).
    const core::ImageSize source =
        image_ && half_ ? core::spreadHalfSize(image_->sourceSize(), *half_) : (image_ ? image_->sourceSize() : core::ImageSize{});
    fitSize_ = core::fitPage(source, screen, fit_);
    drawn_ = scaled(fitSize_, zoom_);
    // Zoomed ("Keep the zoom"), this is the corner where reading starts: top right for right to left, top left otherwise.
    position_ = core::startPosition(drawn_, screen, direction_);
    placed_ = true;
}

void ReaderView::setZoomValue(float zoom) {
    zoom_ = zoom;
    drawn_ = scaled(fitSize_, zoom_);
}

void ReaderView::zoomChanged() {
    ++zoomRevision_;
    zoomChangedMs_ = nowMs();
}

void ReaderView::zoomTo(float zoom, core::PointF anchor) {
    if (!placed_ || !hasPage() || pinch_) {
        return;
    }
    const float to = core::clampZoom(zoom, maxZoom_);
    if (std::fabs(to - zoom_) < 0.0001f) {
        return;
    }
    glide_.reset();
    position_ = core::zoomAround(position_, fitSize_, zoom_, to, anchor, screen_);
    setZoomValue(to);
    zoomChanged();
}

void ReaderView::zoomStep(int direction) {
    zoomTo(core::zoomStep(zoom_, direction, maxZoom_), {screen_.width / 2, screen_.height / 2});
}

void ReaderView::resetZoom() {
    zoomTo(1, {screen_.width / 2, screen_.height / 2});
}

bool ReaderView::canMove(core::Arrow arrow) const {
    if (!placed_) {
        return false;
    }
    return core::canMoveToward(glide_ ? glide_->to : position_, drawn_, screen_, arrow);
}

bool ReaderView::panBy(core::PointF move) {
    if (!placed_ || pinch_) {
        return false;
    }
    glide_.reset();
    const core::PointF to = core::clampPosition({position_.x + move.x, position_.y + move.y}, drawn_, screen_);
    const bool moved = std::fabs(to.x - position_.x) > 0.01f || std::fabs(to.y - position_.y) > 0.01f;
    position_ = to;
    return moved;
}

void ReaderView::glideBy(core::PointF move) {
    if (!placed_ || pinch_) {
        return;
    }
    const core::PointF from = glide_ ? glide_->to : position_;
    const core::PointF to = core::clampPosition({from.x + move.x, from.y + move.y}, drawn_, screen_);
    if (!glide_) {
        gesture("glide", true);
    }
    glide_ = Glide{position_, to, std::chrono::steady_clock::now()};
}

void ReaderView::scrollBy(float dy) {
    if (placed_) {
        position_ = core::clampPosition({position_.x, position_.y - dy}, drawn_, screen_);
        ++moves_;
    }
}

void ReaderView::onFinger(const brls::TouchState& touch) {
    const core::PointF at = toReader(touch.position);
    const int id = touch.fingerId;
    switch (touch.phase) {
        case brls::TouchPhase::START:
            if (fingers_.empty()) {
                multiTouch_ = false;  // a fresh touch
            }
            fingers_[id] = Finger{at, pass_};
            if (fingers_.size() >= 2) {
                multiTouch_ = true;  // from now on until every finger has lifted, no tap, drag or swipe counts
                pendingTap_.reset();
                if (!pinch_ && fingers_.size() == 2 && placed_ && hasPage()) {
                    startPinch();
                }
            }
            break;
        case brls::TouchPhase::STAY: {
            fingers_[id] = Finger{at, pass_};
            if (pinch_ && (id == pinch_->a || id == pinch_->b) && fingers_.count(pinch_->a) && fingers_.count(pinch_->b)) {
                const core::ZoomView view = core::pinchView(pinch_->start, fingers_[pinch_->a].at, fingers_[pinch_->b].at,
                                                            fitSize_, screen_, maxZoom_);
                setZoomValue(view.zoom);
                position_ = view.position;
            }
            break;
        }
        case brls::TouchPhase::END:
            fingers_.erase(id);
            if (pinch_ && (id == pinch_->a || id == pinch_->b)) {
                endPinch();  // one finger lifted: the pinch ends; the other does nothing until it lifts too
            }
            break;
        case brls::TouchPhase::NONE:
            break;
    }
}

void ReaderView::startPinch() {
    if (dragging_) {
        dragging_ = false;
        ++moves_;
        gesture("drag", false);
    }
    glide_.reset();
    auto it = fingers_.begin();
    const auto first = *it++;
    const auto second = *it;
    pinch_ = Pinch{core::PinchStart{zoom_, position_, first.second.at, second.second.at}, first.first, second.first};
    gesture("pinch", true);
}

void ReaderView::endPinch() {
    pinch_.reset();
    if (core::snapsToFit(zoom_)) {
        // Let go just above the fit: the whole page again, centred.
        setZoomValue(1);
        position_ = core::clampPosition(position_, drawn_, screen_);
    }
    zoomChanged();
    ++moves_;
    gesture("pinch", false);
}

void ReaderView::onPan(const brls::PanGestureStatus& status) {
    if (multiTouch_) {
        return;  // two fingers: the pinch has it
    }
    if (status.state == brls::GestureState::UNSURE) {
        // The finger just touched down: borealis does not know yet whether this is a tap or a drag.
        touchDown_ = status.position;
        touchDownAt_ = std::chrono::steady_clock::now();
        swallowTap_ = strip_ != nullptr && strip_->touchDown();  // a touch during a flick stops it and is no tap
        if (pendingTap_) {
            // A middle tap waits: close by and in time this may be the second tap of a double tap; else it was a single tap.
            if (core::isDoubleTap(pendingTap_->at, pendingTap_->upMs, toReader(status.position), nowMs(),
                                  readerScreen().width, kTapMaxSlide, kDoubleTapGapMs)) {
                pendingTap_->secondDown = true;
            } else {
                firePendingTap();
            }
        }
        return;
    }
    if (status.state == brls::GestureState::START) {
        glide_.reset();
        dragFrom_ = position_;
        if (strip_) {
            strip_->dragStart(nowMs());
        }
        dragging_ = true;
        couldMoveLeft_ = placed_ && core::canMoveToward(position_, drawn_, screen_, core::Side::Left);
        couldMoveRight_ = placed_ && core::canMoveToward(position_, drawn_, screen_, core::Side::Right);
        gesture("drag", true);
    }
    if (!dragging_) {
        return;  // the drag began before a pinch took over
    }
    const core::PointF move = core::screenMoveToReader(
        orientation_, {status.position.x - status.startPosition.x, status.position.y - status.startPosition.y});
    const float dx = move.x;
    const float dy = move.y;
    if (strip_ && (status.state == brls::GestureState::START || status.state == brls::GestureState::STAY ||
                   status.state == brls::GestureState::END)) {
        strip_->dragTo(dy, nowMs());
    }
    if (placed_ && (status.state == brls::GestureState::START || status.state == brls::GestureState::STAY ||
                    status.state == brls::GestureState::END)) {
        position_ = core::clampPosition({dragFrom_.x + dx, dragFrom_.y + dy}, drawn_, screen_);
    }
    if (status.state == brls::GestureState::END) {
        dragging_ = false;
        gesture("drag", false);
        // borealis cancelled the tap when this drag began. A quick one that slid only a little was a tap after all.
        const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - touchDownAt_);
        if (core::isTap(status.position.x - touchDown_.x, status.position.y - touchDown_.y,
                        static_cast<int>(held.count()), kTapMaxSlide, kTapMaxHeldMs)) {
            if (placed_) {
                position_ = dragFrom_;  // undo the little move
            }
            if (strip_) {
                strip_->dragTo(0, nowMs());  // the strip too
            }
            onTapped(touchDown_);
            return;
        }
        if (strip_) {
            strip_->dragEnd(nowMs());  // it may flick on; no swipes in the strip
            return;
        }
        ++moves_;
        if (pendingTap_ && pendingTap_->secondDown) {
            pendingTap_.reset();  // the second touch was a drag: neither a double tap nor a menu
        }
        const std::optional<core::Side> side = core::swipeSide(dx, dy, kSwipeMinDistance);
        // A page that could still move that way when the swipe began moved instead of turning.
        if (side && onSwipe && !(*side == core::Side::Left ? couldMoveLeft_ : couldMoveRight_)) {
            onSwipe(*side);
        }
    }
}

void ReaderView::tick() {
    // Fingers borealis said nothing about in the last input pass have lifted (a finger that is down gets a touch every
    // pass), even if their end never came.
    for (auto it = fingers_.begin(); it != fingers_.end();) {
        if (it->second.pass != pass_) {
            const int id = it->first;
            it = fingers_.erase(it);
            if (pinch_ && (id == pinch_->a || id == pinch_->b)) {
                endPinch();
            }
        } else {
            ++it;
        }
    }
    if (glide_) {
        const float elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - glide_->started)
                                  .count();
        const float t = elapsed / static_cast<float>(core::kArrowGlideMs);
        const float p = core::glideProgress(t);
        position_ = {glide_->from.x + (glide_->to.x - glide_->from.x) * p,
                     glide_->from.y + (glide_->to.y - glide_->from.y) * p};
        if (t >= 1) {
            position_ = glide_->to;
            glide_.reset();
            ++moves_;
            gesture("glide", false);
        }
    }
    if (strip_) {
        strip_->tick();  // the flick and the glide, from the clock
    }
    // A single middle tap that no second one followed: the menu now.
    if (pendingTap_ && !pendingTap_->secondDown && nowMs() - pendingTap_->upMs > kDoubleTapGapMs) {
        firePendingTap();
    }
}

void ReaderView::drawSpinner(NVGcontext* vg, float cx, float cy) {
    // Turns with the clock, so it needs no borealis animation (one would keep the app from ever being "idle").
    const auto ms = nowMs();
    const float start = static_cast<float>(std::fmod(static_cast<double>(ms) / 1000.0 * 2 * kPi, 2 * kPi));
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, kSpinnerRadius, start, start + static_cast<float>(kPi * 1.5), NVG_CW);
    nvgStrokeColor(vg, nvgRGB(150, 150, 150));
    nvgStrokeWidth(vg, 4);
    nvgStroke(vg);
}

void ReaderView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                      brls::FrameContext* ctx) {
    tick();
    if (onFrame) {
        onFrame();
    }
    const core::SizeF screen = core::readerScreen(orientation_, core::SizeF{width, height});
    const bool turned = core::isPortrait(orientation_);
    if (turned) {
        // Everything below is drawn in reader units from (x, y), turned onto the real screen.
        const core::Transform t = core::readerTransform(orientation_, {width, height});
        nvgSave(vg);
        nvgTranslate(vg, x, y);
        nvgTransform(vg, t.a, t.b, t.c, t.d, t.e, t.f);
        nvgTranslate(vg, -x, -y);
    }
    if (strip_) {
        strip_->draw(vg, x, y, screen);
    } else {
        drawPage(vg, x, y, screen);
    }
    if (spinner_) {
        drawSpinner(vg, x + screen.width / 2, y + screen.height / 2);
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
    if (turned) {
        nvgRestore(vg);
    }
    ++pass_;  // touches from now on belong to the next input pass
    if (onDrawn) {
        onDrawn();
    }
}

void ReaderView::drawPage(NVGcontext* vg, float x, float y, core::SizeF screen) {
    const float width = screen.width;
    const float height = screen.height;
    if (!textures_.empty()) {
        if (!placed_ || screen_.width != width || screen_.height != height) {
            layout({width, height});
        }
        // The part of the decoded image drawn as the page: all of it, or one half of a split spread (WP-13c). It is drawn
        // drawn_ big with its corner at position_.
        const core::ImageSize decoded = image_->size();
        const core::TileRect part =
            half_ ? core::spreadHalf(decoded, *half_) : core::TileRect{0, 0, decoded.width, decoded.height};
        const float sx = part.width > 0 ? drawn_.width / static_cast<float>(part.width) : 1;
        const float sy = part.height > 0 ? drawn_.height / static_cast<float>(part.height) : 1;
        const float left = x + position_.x;
        const float top = y + position_.y;
        const float right = left + drawn_.width;
        const float bottom = top + drawn_.height;
        const auto& tiles = image_->tiles();
        for (std::size_t i = 0; i < tiles.size() && i < textures_.size(); ++i) {
            const float tx = left + static_cast<float>(tiles[i].x - part.x) * sx;
            const float ty = top + static_cast<float>(tiles[i].y - part.y) * sy;
            const float tw = static_cast<float>(tiles[i].width) * sx;
            const float th = static_cast<float>(tiles[i].height) * sy;
            // The tile cut to the part's box, so the other half never shows. Where that box reaches the screen's edge or
            // beyond, the tile isn't cut there (nothing beyond the edge shows, and a cut right at the edge would draw a faint
            // anti-aliased line). A whole page's tiles are never cut: they make up its box.
            const float x0 = left > x ? std::max(tx, left) : tx;
            const float y0 = top > y ? std::max(ty, top) : ty;
            const float x1 = right < x + width ? std::min(tx + tw, right) : tx + tw;
            const float y1 = bottom < y + height ? std::min(ty + th, bottom) : ty + th;
            if (x1 <= x0 || y1 <= y0 || x0 > x + width || y0 > y + height || x1 < x || y1 < y) {
                continue;  // outside the part, or off screen
            }
            nvgBeginPath(vg);
            nvgRect(vg, x0, y0, x1 - x0, y1 - y0);
            nvgFillPaint(vg, nvgImagePattern(vg, tx, ty, tw, th, 0, textures_[i], 1.0f));
            nvgFill(vg);
        }
    }
}

}  // namespace snx::ui
