#include "ui/reader/ReaderView.hpp"

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
constexpr float kSpinnerRadius = 24;
constexpr double kPi = 3.14159265358979323846;

}  // namespace

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
    addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound*) {
        if (status.state == brls::GestureState::END) {
            onTapped(status.position);
        }
    }));
    addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound*) { onPan(status); }, brls::PanAxis::ANY));
}

void ReaderView::setOrientation(core::ReaderOrientation orientation, core::SizeF screen) {
    orientation_ = orientation;
    real_ = screen;
    const core::SizeF reader = readerScreen();
    overlay_->setWidth(reader.width);
    overlay_->setHeight(reader.height);
    placed_ = false;
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

void ReaderView::onTapped(brls::Point position) {
    const core::PointF at = toReader(position);
    // In portrait every tap comes here. In landscape borealis runs a tapped button itself, but a tap that slid a little
    // (onPan) comes here. The overlay's children are laid out in reader units from this view's corner.
    const brls::Point local{getX() + at.x, getY() + at.y};
    for (const Button& button : buttons_) {
        if (shown(button.view) && button.view->getFrame().pointInside(local)) {
            brls::sync(button.action);  // after the tap is done (the action may close this screen)
            return;
        }
    }
    if (onTap) {
        onTap(core::tapArea(at.x, readerScreen().width));
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

void ReaderView::showPage(const image::ImagePtr& image, core::PageFit fit, core::ReadingDirection direction) {
    if (image == image_ && fit == fit_ && direction == direction_) {
        return;
    }
    const bool newImage = image != image_;
    fit_ = fit;
    direction_ = direction;
    placed_ = false;
    if (!newImage) {
        return;
    }
    freeTextures();
    image_ = image;
    refused_ = false;
    drawn_ = {};
    ++revision_;
    if (!image_) {
        return;
    }
    // One texture per tile (each side <= maxTextureSide), each one reserved in the GPU budget first (S7).
    NVGcontext* vg = brls::Application::getNVGContext();
    std::vector<std::uint8_t> scratch;
    const auto& tiles = image_->tiles();
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        image::TextureLease lease(budget_, image::TextureBudget::bytesFor(tiles[i]));
        const std::uint8_t* pixels = lease.ok() ? image_->tilePixels(i, scratch) : nullptr;
        const int texture = pixels ? nvgCreateImageRGBA(vg, tiles[i].width, tiles[i].height, 0, pixels) : 0;
        if (texture == 0) {
            freeTextures();
            refused_ = true;
            return;
        }
        textures_.push_back(texture);
        leases_.push_back(std::move(lease));
    }
}

void ReaderView::layout(core::SizeF screen) {
    screen_ = screen;
    drawn_ = image_ ? core::fitPage(image_->sourceSize(), screen, fit_) : core::SizeF{};
    position_ = core::startPosition(drawn_, screen, direction_);
    placed_ = true;
}

void ReaderView::scrollBy(float dy) {
    if (placed_) {
        position_ = core::clampPosition({position_.x, position_.y - dy}, drawn_, screen_);
        ++moves_;
    }
}

void ReaderView::onPan(const brls::PanGestureStatus& status) {
    if (status.state == brls::GestureState::UNSURE) {
        // The finger just touched down: borealis does not know yet whether this is a tap or a drag.
        touchDown_ = status.position;
        touchDownAt_ = std::chrono::steady_clock::now();
        return;
    }
    if (status.state == brls::GestureState::START) {
        dragFrom_ = position_;
        couldMoveLeft_ = placed_ && core::canMoveToward(position_, drawn_, screen_, core::Side::Left);
        couldMoveRight_ = placed_ && core::canMoveToward(position_, drawn_, screen_, core::Side::Right);
    }
    const core::PointF move = core::screenMoveToReader(
        orientation_, {status.position.x - status.startPosition.x, status.position.y - status.startPosition.y});
    const float dx = move.x;
    const float dy = move.y;
    if (placed_ && (status.state == brls::GestureState::START || status.state == brls::GestureState::STAY ||
                    status.state == brls::GestureState::END)) {
        position_ = core::clampPosition({dragFrom_.x + dx, dragFrom_.y + dy}, drawn_, screen_);
    }
    if (status.state == brls::GestureState::END) {
        // borealis cancelled the tap when this drag began. A quick one that slid only a little was a tap after all.
        const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - touchDownAt_);
        if (core::isTap(status.position.x - touchDown_.x, status.position.y - touchDown_.y,
                        static_cast<int>(held.count()), kTapMaxSlide, kTapMaxHeldMs)) {
            if (placed_) {
                position_ = dragFrom_;  // undo the little move
            }
            onTapped(touchDown_);
            return;
        }
        ++moves_;
        const std::optional<core::Side> side = core::swipeSide(dx, dy, kSwipeMinDistance);
        // A page that could still move that way when the swipe began moved instead of turning.
        if (side && onSwipe && !(*side == core::Side::Left ? couldMoveLeft_ : couldMoveRight_)) {
            onSwipe(*side);
        }
    }
}

void ReaderView::drawSpinner(NVGcontext* vg, float cx, float cy) {
    // Turns with the clock, so it needs no borealis animation (one would keep the app from ever being "idle").
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    const float start = static_cast<float>(std::fmod(static_cast<double>(ms) / 1000.0 * 2 * kPi, 2 * kPi));
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, kSpinnerRadius, start, start + static_cast<float>(kPi * 1.5), NVG_CW);
    nvgStrokeColor(vg, nvgRGB(150, 150, 150));
    nvgStrokeWidth(vg, 4);
    nvgStroke(vg);
}

void ReaderView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                      brls::FrameContext* ctx) {
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
    drawPage(vg, x, y, screen);
    if (spinner_) {
        drawSpinner(vg, x + screen.width / 2, y + screen.height / 2);
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
    if (turned) {
        nvgRestore(vg);
    }
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
        const core::ImageSize decoded = image_->size();
        const float sx = decoded.width > 0 ? drawn_.width / static_cast<float>(decoded.width) : 1;
        const float sy = decoded.height > 0 ? drawn_.height / static_cast<float>(decoded.height) : 1;
        const auto& tiles = image_->tiles();
        for (std::size_t i = 0; i < tiles.size() && i < textures_.size(); ++i) {
            const float tx = x + position_.x + static_cast<float>(tiles[i].x) * sx;
            const float ty = y + position_.y + static_cast<float>(tiles[i].y) * sy;
            const float tw = static_cast<float>(tiles[i].width) * sx;
            const float th = static_cast<float>(tiles[i].height) * sy;
            if (tx > x + width || ty > y + height || tx + tw < x || ty + th < y) {
                continue;  // off screen
            }
            nvgBeginPath(vg);
            nvgRect(vg, tx, ty, tw, th);
            nvgFillPaint(vg, nvgImagePattern(vg, tx, ty, tw, th, 0, textures_[i], 1.0f));
            nvgFill(vg);
        }
    }
}

}  // namespace snx::ui
