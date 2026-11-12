#include "ui/reader/StripCanvas.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <utility>

namespace snx::ui {

namespace {

constexpr float kSpinnerRadius = 24;
constexpr double kPi = 3.14159265358979323846;
constexpr float kFontSize = 24;
constexpr float kLineGap = 10;
constexpr int kSampleKeepMs = 200;  // the drag's samples kept for the release speed (it looks at the last 100 ms)
const NVGcolor kPlaceholder = nvgRGBA(128, 128, 128, 40);

core::PointF middleOf(core::PointF a, core::PointF b) {
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}

}  // namespace

StripCanvas::StripCanvas(StripModel& model, image::TextureBudget& budget, int tilesAround)
    : model_(model), budget_(budget), tilesAround_(std::max(0, tilesAround)) {}

StripCanvas::~StripCanvas() {
    NVGcontext* vg = brls::Application::getNVGContext();
    for (auto& [key, page] : held_) {
        freePage(vg, page);
    }
    held_.clear();
}

std::int64_t StripCanvas::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int StripCanvas::takeWaitFrames() {
    const int frames = waitFrames_;
    waitFrames_ = 0;
    return frames;
}

core::SizeF StripCanvas::screen() const {
    const core::ImageSize units = model_.units();
    return {static_cast<float>(units.width), static_cast<float>(units.height)};
}

double StripCanvas::drawnWidth() const {
    return model_.stripWidth() * model_.zoom();
}

double StripCanvas::left() const {
    return core::clampStripLeft(left_, drawnWidth(), screen().width);
}

// --- Motion ---

void StripCanvas::endGlide() {
    ++moves_;
    if (!glide_ && !sideGlide_) {
        restOnPixels();
    }
    if (!glide_ && !sideGlide_ && onGesture) {
        onGesture("glide", false);  // both ways done: one glide gesture for a press that went both ways
    }
}

void StripCanvas::stopMotion() {
    if (fling_) {
        fling_.reset();
        ++moves_;
        restOnPixels();
        if (onGesture) {
            onGesture("fling", false);
        }
    }
    if (glide_ || sideGlide_) {
        glide_.reset();
        sideGlide_.reset();
        endGlide();
    }
}

void StripCanvas::restOnPixels() {
    // A flick, a glide stopped part way or a pinch can leave the strip between two screen units (pixels in handheld):
    // the picture blurs a little, and two screenshots of one place differ by a fraction of a pixel. At rest it moves (by
    // less than half a unit) onto whole ones: its top line and its left edge.
    const double zoom = model_.zoom();
    const double top = model_.layout().top();
    const double snapped = std::round(top * zoom) / zoom;
    if (snapped != top) {
        model_.scrollBy(snapped - top);
    }
    left_ = std::round(left());
}

void StripCanvas::finishScroll() {
    ++moves_;
    restOnPixels();
}

bool StripCanvas::touchDown() {
    const bool was = moving();
    stopMotion();
    return was;
}

void StripCanvas::dragStart(std::int64_t ms) {
    stopMotion();
    samples_.clear();
    dragOffset_ = {};
    dragLeft_ = left();
    samples_.push_back({ms, 0});
}

void StripCanvas::dragTo(core::PointF offset, std::int64_t ms) {
    // The finger moving down pulls the strip down: the screen goes up the strip (a screen unit is 1 / zoom strip units).
    model_.scrollBy(-(static_cast<double>(offset.y) - dragOffset_.y) / model_.zoom());
    // Sideways the strip follows the finger where it is wider than the screen (zoomed); else it stays centred.
    left_ = core::clampStripLeft(dragLeft_ + offset.x, drawnWidth(), screen().width);
    dragOffset_ = offset;
    samples_.push_back({ms, offset.y});
    while (samples_.size() > 2 && ms - samples_.front().ms > kSampleKeepMs) {
        samples_.erase(samples_.begin());
    }
}

void StripCanvas::dragEnd(std::int64_t ms) {
    const double finger = core::releaseVelocity(samples_, ms);  // screen units per second
    samples_.clear();
    ++moves_;
    const double speed = core::flingStartSpeed(-finger, screen().height);
    if (speed != 0) {
        fling_ = Fling{speed, nowMs(), 0};
        if (onGesture) {
            onGesture("fling", true);
        }
    } else {
        restOnPixels();
    }
}

void StripCanvas::dragCancel() {
    samples_.clear();
    ++moves_;
}

void StripCanvas::glide(int direction) {
    if (fling_) {
        fling_.reset();
        ++moves_;
        if (onGesture) {
            onGesture("fling", false);
        }
    }
    const bool fresh = !glide_ && !sideGlide_;
    const double add = direction * core::kStripGlideShare * model_.layout().viewHeight();
    const double left = glide_ ? glide_->distance - glide_->done : 0;
    glide_ = Glide{left + add, nowMs(), 0};
    if (fresh && onGesture) {
        onGesture("glide", true);
    }
}

void StripCanvas::glideSideways(int direction) {
    const double from = left();
    const double base = sideGlide_ ? sideGlide_->to : from;
    const double to =
        core::clampStripLeft(base - direction * core::kStripGlideShare * screen().width, drawnWidth(), screen().width);
    if (!sideGlide_ && std::fabs(to - from) < 0.5) {
        return;  // nothing more to see that way (or it fits the screen: not zoomed)
    }
    if (fling_) {
        fling_.reset();
        ++moves_;
        if (onGesture) {
            onGesture("fling", false);
        }
    }
    const bool fresh = !glide_ && !sideGlide_;
    sideGlide_ = SideGlide{from, to, nowMs()};
    if (fresh && onGesture) {
        onGesture("glide", true);
    }
}

double StripCanvas::scroll(double dy) {
    stopMotion();
    return model_.scrollBy(dy);
}

double StripCanvas::panBy(double dx) {
    if (sideGlide_) {
        sideGlide_.reset();
        endGlide();
    }
    const double before = left();
    left_ = core::clampStripLeft(before - dx, drawnWidth(), screen().width);
    return before - left();
}

void StripCanvas::tick() {
    const std::int64_t now = nowMs();
    if (fling_) {
        const double seconds = static_cast<double>(now - fling_->startMs) / 1000.0;
        const double offset = core::flingOffset(fling_->velocity, seconds);  // screen units: it feels the same zoomed
        const double step = offset - fling_->done;
        const double moved = model_.scrollBy(step / model_.zoom());
        fling_->done = offset;
        if (core::flingDone(fling_->velocity, seconds) || (step != 0 && moved == 0)) {  // slowed down, or an end
            fling_.reset();
            ++moves_;
            restOnPixels();
            if (onGesture) {
                onGesture("fling", false);
            }
        }
    }
    if (glide_) {
        const float t = static_cast<float>(now - glide_->startMs) / static_cast<float>(core::kStripGlideMs);
        const double target = glide_->distance * core::glideProgress(std::min(t, 1.0f));
        const double step = target - glide_->done;
        const double moved = model_.scrollBy(step);
        glide_->done = target;
        if (t >= 1 || (step != 0 && moved == 0)) {
            glide_.reset();
            endGlide();
        }
    }
    if (sideGlide_) {
        const float t = static_cast<float>(now - sideGlide_->startMs) / static_cast<float>(core::kStripGlideMs);
        left_ = sideGlide_->from + (sideGlide_->to - sideGlide_->from) * core::glideProgress(std::min(t, 1.0f));
        if (t >= 1) {
            left_ = sideGlide_->to;
            sideGlide_.reset();
            endGlide();
        }
    }
}

// --- Zoom (WP-13d) ---

void StripCanvas::zoomTo(float zoom, core::PointF anchor) {
    if (!ready() || pinch_) {
        return;
    }
    const float to = core::clampZoom(zoom, model_.maxZoom());
    if (std::fabs(to - model_.zoom()) < 0.0001f) {
        return;
    }
    stopMotion();
    const core::StripZoomView view =
        core::stripZoomAround({model_.zoom(), left(), model_.layout().top()}, to, anchor, model_.stripWidth(),
                              screen().width, model_.maxZoom());
    left_ = view.left;
    model_.setView(view.zoom, view.top);
    restOnPixels();
    ++zoomRevision_;
}

void StripCanvas::zoomStep(int direction) {
    const core::SizeF s = screen();
    zoomTo(core::zoomStep(model_.zoom(), direction, model_.maxZoom()), {s.width / 2, s.height / 2});
}

void StripCanvas::pinchStart(core::PointF a, core::PointF b) {
    if (!ready()) {
        return;
    }
    stopMotion();
    pinch_ = core::StripPinchStart{{model_.zoom(), left(), model_.layout().top()}, a, b};
    pinchAt_ = middleOf(a, b);
}

void StripCanvas::pinchTo(core::PointF a, core::PointF b) {
    if (!pinch_) {
        return;
    }
    const core::StripZoomView view =
        core::stripPinchView(*pinch_, a, b, model_.stripWidth(), screen().width, model_.maxZoom());
    left_ = view.left;
    model_.setView(view.zoom, view.top);
    pinchAt_ = middleOf(a, b);
}

void StripCanvas::pinchEnd() {
    if (!pinch_) {
        return;
    }
    pinch_.reset();
    if (core::snapsToFit(model_.zoom()) && core::isZoomed(model_.zoom())) {
        // Let go just above 1: the strip's width again, around where the fingers were.
        const core::StripZoomView view = core::stripZoomAround({model_.zoom(), left(), model_.layout().top()}, 1,
                                                               pinchAt_, model_.stripWidth(), screen().width,
                                                               model_.maxZoom());
        left_ = view.left;
        model_.setView(view.zoom, view.top);
    }
    restOnPixels();
    ++zoomRevision_;
    ++moves_;
}

// --- Textures ---

int StripCanvas::tilesHeld() const {
    int count = 0;
    for (const auto& [key, page] : held_) {
        count += static_cast<int>(page.tiles.size());
    }
    return count;
}

core::ImageSize StripCanvas::drawnSize(int item) const {
    const core::StripItem& it = model_.layout().item(item);
    const auto found = held_.find(PageKey{it.chapterId, it.page});
    return found != held_.end() && it.kind == core::StripItemKind::Page ? found->second.decoded : core::ImageSize{};
}

void StripCanvas::freeTile(NVGcontext* vg, Held& held) {
    if (held.texture != 0 && vg != nullptr) {
        nvgDeleteImage(vg, held.texture);
    }
    held.texture = 0;
}

void StripCanvas::freePage(NVGcontext* vg, HeldPage& page) {
    for (auto& [index, held] : page.tiles) {
        freeTile(vg, held);
    }
    page.tiles.clear();  // the leases go with them
}

bool StripCanvas::makeTexture(NVGcontext* vg, const image::ImagePtr& image, int tile, Held& out) {
    const core::TileRect& rect = image->tiles()[static_cast<std::size_t>(tile)];
    image::TextureLease lease(budget_, image::TextureBudget::bytesFor(rect));
    if (!lease.ok()) {
        return false;
    }
    std::vector<std::uint8_t> scratch;
    const std::uint8_t* pixels = image->tilePixels(static_cast<std::size_t>(tile), scratch);
    const int texture = pixels ? nvgCreateImageRGBA(vg, rect.width, rect.height, 0, pixels) : 0;
    if (texture == 0) {
        return false;
    }
    out.texture = texture;
    out.lease = std::move(lease);
    return true;
}

bool StripCanvas::upload(NVGcontext* vg, const Key& key, const image::ImagePtr& image, const std::vector<Key>& keepOrder) {
    const core::TileRect& rect = image->tiles()[static_cast<std::size_t>(key.tile)];
    image::TextureLease lease(budget_, image::TextureBudget::bytesFor(rect));
    // No room: the held tiles farthest from the screen (the end of the window's order) make room, never this one's betters.
    for (auto later = keepOrder.rbegin(); !lease.ok() && later != keepOrder.rend(); ++later) {
        if (*later == key) {
            break;  // reached this tile: everything before it in the order is more urgent
        }
        const auto page = held_.find(later->page);
        if (page == held_.end()) {
            continue;
        }
        const auto tile = page->second.tiles.find(later->tile);
        if (tile != page->second.tiles.end()) {
            freeTile(vg, tile->second);
            page->second.tiles.erase(tile);
            lease = image::TextureLease(budget_, image::TextureBudget::bytesFor(rect));
        }
    }
    if (!lease.ok()) {
        return false;
    }
    std::vector<std::uint8_t> scratch;
    const std::uint8_t* pixels = image->tilePixels(static_cast<std::size_t>(key.tile), scratch);
    const int texture = pixels ? nvgCreateImageRGBA(vg, rect.width, rect.height, 0, pixels) : 0;
    if (texture == 0) {
        return false;
    }
    Held held;
    held.texture = texture;
    held.lease = std::move(lease);
    held_[key.page].tiles.emplace(key.tile, std::move(held));
    return true;
}

bool StripCanvas::swapTo(NVGcontext* vg, HeldPage& held, const image::ImagePtr& image, int item) {
    // A sharper decode of a page (WP-13d): its tiles on screen are made first, while the old ones stay drawn; only when all
    // of them are in do the old ones go (S7: both are reserved for that moment). No room: the old ones stay, blurrier.
    const core::StripLayout& layout = model_.layout();
    const std::vector<core::StripTile> onScreen = core::stripTileWindow(
        {{item, layout.itemTop(item), layout.item(item).height, image->size(), image->tiles()}}, layout.top(),
        layout.bottom(), 0);
    const auto started = std::chrono::steady_clock::now();
    HeldPage fresh;
    fresh.decoded = image->size();
    fresh.rects = image->tiles();
    for (const core::StripTile& tile : onScreen) {
        if (!tile.onScreen) {
            continue;
        }
        Held made;
        if (!makeTexture(vg, image, tile.tile, made)) {
            freePage(vg, fresh);
            held.refused = image->size();
            return false;
        }
        fresh.tiles.emplace(tile.tile, std::move(made));
    }
    freePage(vg, held);
    held = std::move(fresh);
    ++swaps_;
    swapUploadMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    return true;
}

void StripCanvas::updateTextures(NVGcontext* vg) {
    const core::StripLayout& layout = model_.layout();
    std::vector<core::StripPageTiles> pages;
    std::map<int, image::ImagePtr> sources;  // by layout item: the decode to upload that page's missing tiles from
    std::set<PageKey> present;
    const auto started = std::chrono::steady_clock::now();
    bool any = false;
    for (int item = 0; item < layout.size(); ++item) {
        const image::ImagePtr want = model_.shownImage(item);
        if (!want) {
            continue;
        }
        const core::StripItem& it = layout.item(item);
        const PageKey key{it.chapterId, it.page};
        present.insert(key);
        auto found = held_.find(key);
        if (found == held_.end()) {
            HeldPage page;
            page.decoded = want->size();
            page.rects = want->tiles();
            found = held_.emplace(key, std::move(page)).first;
        }
        HeldPage& held = found->second;
        if (held.decoded == want->size()) {
            sources[item] = want;
        } else if (want->size().width > held.decoded.width) {
            // A sharper decode: swap over once its tiles on screen are made; until then (or if it found no room) the old
            // decode stays drawn, its missing tiles uploaded from the page's own image while that is the one it shows.
            if (held.refused != want->size()) {
                any = true;
                if (swapTo(vg, held, want, item)) {
                    sources[item] = want;
                }
            }
            const image::ImagePtr& own = model_.pageAt(item).image;
            if (held.decoded != want->size() && own && own->size() == held.decoded) {
                sources[item] = own;
            }
        } else {
            // Back to a smaller decode (zoom 1, or the sharper one let go): the old textures go first.
            freePage(vg, held);
            held.decoded = want->size();
            held.rects = want->tiles();
            held.refused = {};
            ++swaps_;
            sources[item] = want;
        }
        pages.push_back({item, layout.itemTop(item), it.height, held.decoded, held.rects});
    }
    // The pages the model let go (or that left the strip): their textures go.
    for (auto it = held_.begin(); it != held_.end();) {
        if (present.count(it->first) == 0) {
            freePage(vg, it->second);
            it = held_.erase(it);
        } else {
            ++it;
        }
    }
    const std::vector<core::StripTile> window = core::stripTileWindow(pages, layout.top(), layout.bottom(), tilesAround_);
    std::vector<Key> order;
    order.reserve(window.size());
    for (const core::StripTile& tile : window) {
        const core::StripItem& item = layout.item(tile.item);
        order.push_back({PageKey{item.chapterId, item.page}, tile.tile});
    }
    const std::set<Key> keep(order.begin(), order.end());
    // Free the tiles that left the window.
    for (auto& [key, page] : held_) {
        for (auto tile = page.tiles.begin(); tile != page.tiles.end();) {
            if (keep.count(Key{key, tile->first}) == 0) {
                freeTile(vg, tile->second);
                tile = page.tiles.erase(tile);
            } else {
                ++tile;
            }
        }
    }
    // The tiles on screen at once; one more of the others per frame, nearest first.
    refused_ = false;
    bool otherDone = false;
    for (std::size_t i = 0; i < window.size(); ++i) {
        const auto page = held_.find(order[i].page);
        if (page == held_.end() || page->second.tiles.count(order[i].tile) > 0) {
            continue;
        }
        if (!window[i].onScreen) {
            if (otherDone) {
                continue;
            }
            otherDone = true;
        }
        const auto source = sources.find(window[i].item);
        if (source == sources.end()) {
            continue;  // the decode these textures come from is gone: a placeholder there until the page swaps over
        }
        any = true;
        if (!upload(vg, order[i], source->second, order) && window[i].onScreen) {
            refused_ = true;
        }
    }
    if (any) {
        uploadMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
}

// --- Drawing ---

void StripCanvas::drawSpinner(NVGcontext* vg, float cx, float cy) {
    const auto ms = nowMs();
    const float start = static_cast<float>(std::fmod(static_cast<double>(ms) / 1000.0 * 2 * kPi, 2 * kPi));
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, kSpinnerRadius, start, start + static_cast<float>(kPi * 1.5), NVG_CW);
    nvgStrokeColor(vg, nvgRGB(150, 150, 150));
    nvgStrokeWidth(vg, 4);
    nvgStroke(vg);
}

void StripCanvas::drawText(NVGcontext* vg, const std::vector<std::string>& lines, float cx, float cy, float maxWidth) {
    if (lines.empty()) {
        return;
    }
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, kFontSize);
    nvgTextLineHeight(vg, 1.2f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(vg, textColor_);
    std::vector<float> heights;
    float total = 0;
    for (const std::string& line : lines) {
        float bounds[4] = {0, 0, 0, 0};
        nvgTextBoxBounds(vg, 0, 0, maxWidth, line.c_str(), nullptr, bounds);
        heights.push_back(bounds[3] - bounds[1]);
        total += heights.back();
    }
    total += kLineGap * static_cast<float>(lines.size() - 1);
    float top = cy - total / 2;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        nvgTextBox(vg, cx - maxWidth / 2, top, maxWidth, lines[i].c_str(), nullptr);
        top += heights[i] + kLineGap;
    }
}

void StripCanvas::drawItem(NVGcontext* vg, int item, float left, float top, float width, float height,
                           core::SizeF screen, float x, float y, bool& waiting) {
    const core::StripLayout& layout = model_.layout();
    const core::StripItem& it = layout.item(item);
    // Text and the spinner go in the middle of the part of the item that shows (zoomed, much of it lies off screen); the
    // text keeps its size at any zoom.
    const float visibleTop = std::max(top, y);
    const float visibleBottom = std::min(top + height, y + screen.height);
    const float visibleLeft = std::max(left, x);
    const float visibleRight = std::min(left + width, x + screen.width);
    const float middle = (visibleTop + visibleBottom) / 2;
    const float cx = (visibleLeft + visibleRight) / 2;
    const float textWidth = std::max(visibleRight - visibleLeft - 40, 200.0f);
    if (it.kind == core::StripItemKind::Band) {
        if (textFor) {
            drawText(vg, textFor(item), cx, middle, textWidth);
        }
        return;
    }
    const ReaderPage& page = model_.pageAt(item);
    if (page.state == ReaderPage::State::Failed) {
        nvgBeginPath(vg);
        nvgRect(vg, visibleLeft, visibleTop, visibleRight - visibleLeft, visibleBottom - visibleTop);
        nvgFillColor(vg, kPlaceholder);
        nvgFill(vg);
        if (textFor) {
            drawText(vg, textFor(item), cx, middle, textWidth);
        }
        return;
    }
    const auto found = held_.find(PageKey{it.chapterId, it.page});
    if (page.state != ReaderPage::State::Ready || !page.image || found == held_.end()) {
        nvgBeginPath(vg);
        nvgRect(vg, visibleLeft, visibleTop, visibleRight - visibleLeft, visibleBottom - visibleTop);
        nvgFillColor(vg, kPlaceholder);
        nvgFill(vg);
        if (page.state != ReaderPage::State::Ready) {
            drawSpinner(vg, cx, middle);
        }
        waiting = true;
        return;
    }
    // Drawn from the decode its textures come from (the page's own, or a sharper one).
    const HeldPage& held = found->second;
    const float sx = held.decoded.width > 0 ? width / static_cast<float>(held.decoded.width) : 1;
    const float sy = held.decoded.height > 0 ? height / static_cast<float>(held.decoded.height) : 1;
    for (std::size_t i = 0; i < held.rects.size(); ++i) {
        const core::TileRect& rect = held.rects[i];
        const float tx = left + static_cast<float>(rect.x) * sx;
        const float ty = top + static_cast<float>(rect.y) * sy;
        const float tw = static_cast<float>(rect.width) * sx;
        const float th = static_cast<float>(rect.height) * sy;
        if (ty > y + screen.height || ty + th < y || tx > x + screen.width || tx + tw < x) {
            continue;  // off screen
        }
        const auto tile = held.tiles.find(static_cast<int>(i));
        nvgBeginPath(vg);
        nvgRect(vg, tx, ty, tw, th);
        if (tile == held.tiles.end()) {
            nvgFillColor(vg, kPlaceholder);  // not uploaded yet (or no room): a placeholder this frame
            waiting = true;
        } else {
            nvgFillPaint(vg, nvgImagePattern(vg, tx, ty, tw, th, 0, tile->second.texture, 1.0f));
        }
        nvgFill(vg);
    }
}

void StripCanvas::draw(NVGcontext* vg, float x, float y, core::SizeF screen) {
    if (!ready()) {
        return;
    }
    updateTextures(vg);
    const core::StripLayout& layout = model_.layout();
    // A strip unit is `zoom` reader units: the strip is drawn zoom times its width, from its left edge (centred while it
    // fits; the sides show the reader's background).
    const double zoom = model_.zoom();
    const float width = static_cast<float>(drawnWidth());
    const float left = x + static_cast<float>(this->left());
    const auto [first, last] = layout.overlapping(layout.top(), layout.bottom());
    bool waiting = false;
    for (int item = first; item < last; ++item) {
        const float top = y + static_cast<float>((layout.itemTop(item) - layout.top()) * zoom);
        const float height = static_cast<float>(layout.item(item).height * zoom);
        drawItem(vg, item, left, top, width, height, screen, x, y, waiting);
    }
    waitingNow_ = waiting;
    if (waiting) {
        ++waitFrames_;
    }
}

}  // namespace snx::ui
