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

}  // namespace

StripCanvas::StripCanvas(StripModel& model, image::TextureBudget& budget, int tilesAround)
    : model_(model), budget_(budget), tilesAround_(std::max(0, tilesAround)) {}

StripCanvas::~StripCanvas() {
    NVGcontext* vg = brls::Application::getNVGContext();
    for (auto& [key, held] : held_) {
        if (held.texture != 0 && vg != nullptr) {
            nvgDeleteImage(vg, held.texture);
        }
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

// --- Motion ---

void StripCanvas::stopMotion() {
    if (fling_) {
        fling_.reset();
        ++moves_;
        if (onGesture) {
            onGesture("fling", false);
        }
    }
    if (glide_) {
        glide_.reset();
        ++moves_;
        if (onGesture) {
            onGesture("glide", false);
        }
    }
}

bool StripCanvas::touchDown() {
    const bool was = moving();
    stopMotion();
    return was;
}

void StripCanvas::dragStart(std::int64_t ms) {
    stopMotion();
    samples_.clear();
    dragOffset_ = 0;
    samples_.push_back({ms, 0});
}

void StripCanvas::dragTo(float offsetY, std::int64_t ms) {
    // The finger moving down pulls the strip down: the screen goes up the strip.
    model_.scrollBy(-(static_cast<double>(offsetY) - dragOffset_));
    dragOffset_ = offsetY;
    samples_.push_back({ms, offsetY});
    while (samples_.size() > 2 && ms - samples_.front().ms > kSampleKeepMs) {
        samples_.erase(samples_.begin());
    }
}

void StripCanvas::dragEnd(std::int64_t ms) {
    const double finger = core::releaseVelocity(samples_, ms);
    samples_.clear();
    ++moves_;
    const double speed = core::flingStartSpeed(-finger, model_.layout().viewHeight());
    if (speed != 0) {
        fling_ = Fling{speed, nowMs(), 0};
        if (onGesture) {
            onGesture("fling", true);
        }
    }
}

void StripCanvas::glide(int direction) {
    if (fling_) {
        fling_.reset();
        ++moves_;
        if (onGesture) {
            onGesture("fling", false);
        }
    }
    const double add = direction * core::kStripGlideShare * model_.layout().viewHeight();
    const bool fresh = !glide_;
    const double left = glide_ ? glide_->distance - glide_->done : 0;
    glide_ = Glide{left + add, nowMs(), 0};
    if (fresh && onGesture) {
        onGesture("glide", true);
    }
}

double StripCanvas::scroll(double dy) {
    stopMotion();
    return model_.scrollBy(dy);
}

void StripCanvas::tick() {
    const std::int64_t now = nowMs();
    if (fling_) {
        const double seconds = static_cast<double>(now - fling_->startMs) / 1000.0;
        const double offset = core::flingOffset(fling_->velocity, seconds);
        const double step = offset - fling_->done;
        const double moved = model_.scrollBy(step);
        fling_->done = offset;
        if (core::flingDone(fling_->velocity, seconds) || (step != 0 && moved == 0)) {  // slowed down, or an end
            fling_.reset();
            ++moves_;
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
            ++moves_;
            if (onGesture) {
                onGesture("glide", false);
            }
        }
    }
}

// --- Textures ---

void StripCanvas::freeTexture(NVGcontext* vg, std::map<Key, Held>::iterator it) {
    if (it->second.texture != 0 && vg != nullptr) {
        nvgDeleteImage(vg, it->second.texture);
    }
    held_.erase(it);
}

bool StripCanvas::upload(NVGcontext* vg, const Key& key, const image::ImagePtr& image, std::size_t tile,
                         const std::vector<Key>& keepOrder) {
    const core::TileRect& rect = image->tiles()[tile];
    image::TextureLease lease(budget_, image::TextureBudget::bytesFor(rect));
    // No room: the held tiles farthest from the screen (the end of the window's order) make room, never this one's betters.
    for (auto later = keepOrder.rbegin(); !lease.ok() && later != keepOrder.rend(); ++later) {
        if (!(*later < key) && !(key < *later)) {
            break;  // reached this tile: everything before it in the order is more urgent
        }
        const auto it = held_.find(*later);
        if (it != held_.end()) {
            freeTexture(vg, it);
            lease = image::TextureLease(budget_, image::TextureBudget::bytesFor(rect));
        }
    }
    if (!lease.ok()) {
        return false;
    }
    std::vector<std::uint8_t> scratch;
    const std::uint8_t* pixels = image->tilePixels(tile, scratch);
    const int texture = pixels ? nvgCreateImageRGBA(vg, rect.width, rect.height, 0, pixels) : 0;
    if (texture == 0) {
        return false;
    }
    Held held;
    held.texture = texture;
    held.lease = std::move(lease);
    held.decoded = image->size();
    held_.emplace(key, std::move(held));
    return true;
}

void StripCanvas::updateTextures(NVGcontext* vg) {
    const core::StripLayout& layout = model_.layout();
    std::vector<core::StripPageTiles> pages;
    std::map<int, image::ImagePtr> images;  // by layout item
    for (int item = 0; item < layout.size(); ++item) {
        const ReaderPage& page = model_.pageAt(item);
        if (page.state == ReaderPage::State::Ready && page.image) {
            pages.push_back({item, layout.itemTop(item), layout.item(item).height, page.image->size(), page.image->tiles()});
            images[item] = page.image;
        }
    }
    const std::vector<core::StripTile> window = core::stripTileWindow(pages, layout.top(), layout.bottom(), tilesAround_);
    std::vector<Key> order;
    order.reserve(window.size());
    for (const core::StripTile& tile : window) {
        const core::StripItem& item = layout.item(tile.item);
        order.push_back({item.chapterId, item.page, tile.tile});
    }
    const std::set<Key> keep(order.begin(), order.end());
    // Free what left the window, and the tiles of an image that changed (the page decoded again at another size).
    for (auto it = held_.begin(); it != held_.end();) {
        const auto next = std::next(it);
        bool stale = keep.count(it->first) == 0;
        if (!stale) {
            const int item = layout.find(it->first.chapterId, it->first.page);
            const auto found = images.find(item);
            stale = found == images.end() || found->second->size() != it->second.decoded;
        }
        if (stale) {
            freeTexture(vg, it);
        }
        it = next;
    }
    // The tiles on screen at once; one more of the others per frame, nearest first.
    refused_ = false;
    bool otherDone = false;
    const auto started = std::chrono::steady_clock::now();
    bool any = false;
    for (std::size_t i = 0; i < window.size(); ++i) {
        if (held_.count(order[i]) > 0) {
            continue;
        }
        if (!window[i].onScreen) {
            if (otherDone) {
                continue;
            }
            otherDone = true;
        }
        const auto found = images.find(window[i].item);
        if (found == images.end()) {
            continue;
        }
        any = true;
        if (!upload(vg, order[i], found->second, static_cast<std::size_t>(window[i].tile), order) && window[i].onScreen) {
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
                           core::SizeF screen, float y, bool& waiting) {
    const core::StripLayout& layout = model_.layout();
    const core::StripItem& it = layout.item(item);
    const float visibleTop = std::max(top, y);
    const float visibleBottom = std::min(top + height, y + screen.height);
    const float middle = (visibleTop + visibleBottom) / 2;
    const float cx = left + width / 2;
    const float textWidth = std::max(width - 40, 200.0f);
    if (it.kind == core::StripItemKind::Band) {
        if (textFor) {
            drawText(vg, textFor(item), cx, middle, textWidth);
        }
        return;
    }
    const ReaderPage& page = model_.pageAt(item);
    if (page.state == ReaderPage::State::Failed) {
        nvgBeginPath(vg);
        nvgRect(vg, left, top, width, height);
        nvgFillColor(vg, kPlaceholder);
        nvgFill(vg);
        if (textFor) {
            drawText(vg, textFor(item), cx, middle, textWidth);
        }
        return;
    }
    if (page.state != ReaderPage::State::Ready || !page.image) {
        nvgBeginPath(vg);
        nvgRect(vg, left, top, width, height);
        nvgFillColor(vg, kPlaceholder);
        nvgFill(vg);
        drawSpinner(vg, cx, middle);
        waiting = true;
        return;
    }
    const core::ImageSize decoded = page.image->size();
    const float sx = decoded.width > 0 ? width / static_cast<float>(decoded.width) : 1;
    const float sy = decoded.height > 0 ? height / static_cast<float>(decoded.height) : 1;
    const auto& tiles = page.image->tiles();
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const float tx = left + static_cast<float>(tiles[i].x) * sx;
        const float ty = top + static_cast<float>(tiles[i].y) * sy;
        const float tw = static_cast<float>(tiles[i].width) * sx;
        const float th = static_cast<float>(tiles[i].height) * sy;
        if (ty > y + screen.height || ty + th < y) {
            continue;  // off screen
        }
        const auto found = held_.find(Key{it.chapterId, it.page, static_cast<int>(i)});
        nvgBeginPath(vg);
        nvgRect(vg, tx, ty, tw, th);
        if (found == held_.end()) {
            nvgFillColor(vg, kPlaceholder);  // not uploaded yet (or no room): a placeholder this frame
            waiting = true;
        } else {
            nvgFillPaint(vg, nvgImagePattern(vg, tx, ty, tw, th, 0, found->second.texture, 1.0f));
        }
        nvgFill(vg);
    }
}

void StripCanvas::draw(NVGcontext* vg, float x, float y, core::SizeF screen) {
    if (model_.state() != StripModel::State::Ready) {
        return;
    }
    updateTextures(vg);
    const core::StripLayout& layout = model_.layout();
    const float width = static_cast<float>(model_.stripWidth());
    const float left = x + (screen.width - width) / 2;  // centred; the sides show the reader's background
    // The strip's units are the reader's units at zoom 1 (zoom in the strip is WP-13d).
    const auto [first, last] = layout.overlapping(layout.top(), layout.bottom());
    bool waiting = false;
    for (int item = first; item < last; ++item) {
        const float top = y + static_cast<float>(layout.itemTop(item) - layout.top());
        drawItem(vg, item, left, top, width, static_cast<float>(layout.item(item).height), screen, y, waiting);
    }
    waitingNow_ = waiting;
    if (waiting) {
        ++waitFrames_;
    }
}

}  // namespace snx::ui
