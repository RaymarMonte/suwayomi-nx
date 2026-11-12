#include "core/StripMath.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace snx::core {

namespace {

constexpr double kMinItemHeight = 1;  // an item is never flat: every strip y belongs to exactly one item

double widthShare(StripWidth setting) {
    switch (setting) {
        case StripWidth::ThreeQuarters:
            return 0.75;
        case StripWidth::Full:
            return 1.0;
        case StripWidth::Half:
            break;
    }
    return 0.5;
}

double flingRate() {
    return std::log(2.0) / kFlingHalfLifeSeconds;  // the speed is v * e^(-rate * t)
}

}  // namespace

// --- Width and decode boxes ---

float stripWidth(StripWidth setting, ReaderOrientation orientation, float readerScreenWidth) {
    if (isPortrait(orientation)) {
        return readerScreenWidth;
    }
    return static_cast<float>(readerScreenWidth * widthShare(setting));
}

ImageSize stripDecodeBox(StripWidth setting, ReaderOrientation orientation, ImageSize readerScreen) {
    const float width = stripWidth(setting, orientation, static_cast<float>(readerScreen.width));
    return {std::max(1, static_cast<int>(std::lround(width))), 0};
}

ImageSize stripZoomDecodeBox(StripWidth setting, ReaderOrientation orientation, ImageSize readerScreen, float zoom,
                             float maxZoom, float maxDecodeScale) {
    const ImageSize plain = stripDecodeBox(setting, orientation, readerScreen);
    return zoomDecodeBox(PageFit::Width, {plain.width, readerScreen.height}, zoom, maxZoom, maxDecodeScale);
}

double stripPageHeight(ImageSize source, double stripWidth) {
    if (source.width <= 0 || source.height <= 0) {
        return std::max(kMinItemHeight, stripWidth * kUnknownPageShape);
    }
    return std::max(kMinItemHeight, stripWidth * source.height / source.width);
}

double estimatedPageHeight(std::optional<ImageSize> lastKnownShape, double stripWidth) {
    if (lastKnownShape && lastKnownShape->width > 0 && lastKnownShape->height > 0) {
        return stripPageHeight(*lastKnownShape, stripWidth);
    }
    return std::max(kMinItemHeight, stripWidth * kUnknownPageShape);
}

double stripBandHeight(double readerScreenHeight) {
    return std::max(kMinItemHeight, readerScreenHeight / 3);
}

// --- The layout ---

const StripItem& StripLayout::item(int index) const {
    static const StripItem kNone{StripItemKind::Band, 0, -1, 0};
    if (index < 0 || index >= size()) {
        return kNone;
    }
    return items_[static_cast<std::size_t>(index)];
}

double StripLayout::itemTop(int index) const {
    if (index < 0 || index >= size()) {
        return 0;
    }
    return tops_[static_cast<std::size_t>(index)];
}

double StripLayout::itemBottom(int index) const {
    if (index < 0 || index >= size()) {
        return 0;
    }
    return tops_[static_cast<std::size_t>(index) + 1];
}

int StripLayout::itemAt(double y) const {
    if (items_.empty()) {
        return -1;
    }
    if (y <= 0) {
        return 0;
    }
    if (y >= contentHeight()) {
        return size() - 1;
    }
    // tops_ = [0, h0, h0 + h1, ..., total]: the last top at or above y.
    const auto it = std::upper_bound(tops_.begin(), tops_.end(), y);
    return std::clamp(static_cast<int>(it - tops_.begin()) - 1, 0, size() - 1);
}

std::pair<int, int> StripLayout::overlapping(double y0, double y1) const {
    if (items_.empty() || y1 <= y0 || y1 <= 0 || y0 >= contentHeight()) {
        return {0, 0};
    }
    const int first = itemAt(y0);
    // The first item whose top is at or below y1 is the first that doesn't overlap.
    const auto end = tops_.begin() + size();
    const int last = static_cast<int>(std::lower_bound(tops_.begin(), end, y1) - tops_.begin());
    return {first, std::max(first, last)};
}

int StripLayout::find(int chapterId, int page) const {
    for (int i = 0; i < size(); ++i) {
        const StripItem& it = items_[static_cast<std::size_t>(i)];
        if (it.kind == StripItemKind::Page && it.chapterId == chapterId && it.page == page) {
            return i;
        }
    }
    return -1;
}

double StripLayout::clampTop(double top) const {
    const double highest = std::max(0.0, contentHeight() - viewHeight_);
    if (!(top > 0)) {  // also NaN
        return 0;
    }
    return std::min(top, highest);
}

double StripLayout::scrollTo(double top) {
    pin_.reset();
    const double before = top_;
    top_ = clampTop(top);
    return top_ - before;
}

StripPlace StripLayout::placeAt(double screenY) const {
    if (items_.empty()) {
        return {};
    }
    const double y = std::clamp(top_ + screenY, 0.0, contentHeight());
    const int index = itemAt(y);
    const double height = itemBottom(index) - itemTop(index);
    const double share = height > 0 ? (y - itemTop(index)) / height : 0;
    return {index, std::clamp(share, 0.0, 1.0)};
}

double StripLayout::topFor(StripPlace place, double screenY) const {
    if (place.item < 0 || place.item >= size()) {
        return clampTop(top_);
    }
    const double height = itemBottom(place.item) - itemTop(place.item);
    const double y = itemTop(place.item) + std::clamp(place.share, 0.0, 1.0) * height;
    return clampTop(y - screenY);
}

void StripLayout::pin(StripPlace place, double screenY) {
    pin_ = Anchor{place, screenY};
    top_ = topFor(place, screenY);
}

StripLayout::Anchor StripLayout::anchor() const {
    if (pin_) {
        return *pin_;
    }
    return Anchor{middle(), viewHeight_ / 2};
}

void StripLayout::restore(const Anchor& anchor) {
    if (anchor.place.item < 0 || items_.empty()) {
        top_ = clampTop(top_);
        return;
    }
    top_ = topFor(anchor.place, anchor.screenY);
}

void StripLayout::rebuild() {
    tops_.assign(items_.size() + 1, 0.0);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        tops_[i + 1] = tops_[i] + items_[i].height;
    }
}

void StripLayout::setViewHeight(double height) {
    height = std::max(kMinItemHeight, height);
    Anchor a = anchor();
    // The anchor's line moves with the screen: the middle stays the middle, the top the top.
    a.screenY = viewHeight_ > 0 ? a.screenY * height / viewHeight_ : height / 2;
    if (pin_) {
        pin_->screenY = a.screenY;
    }
    viewHeight_ = height;
    restore(a);
}

void StripLayout::setHeight(int index, double height) {
    if (index < 0 || index >= size()) {
        return;
    }
    const Anchor a = anchor();
    items_[static_cast<std::size_t>(index)].height = std::max(kMinItemHeight, height);
    rebuild();
    restore(a);
}

void StripLayout::insert(int index, std::vector<StripItem> items) {
    if (items.empty()) {
        return;
    }
    index = std::clamp(index, 0, size());
    const int count = static_cast<int>(items.size());
    Anchor a = anchor();
    if (a.place.item >= index) {
        a.place.item += count;
    }
    if (pin_) {
        pin_->place.item = a.place.item;
    }
    for (StripItem& it : items) {
        it.height = std::max(kMinItemHeight, it.height);
    }
    items_.insert(items_.begin() + index, items.begin(), items.end());
    rebuild();
    restore(a);
}

void StripLayout::erase(int first, int last) {
    first = std::clamp(first, 0, size());
    last = std::clamp(last, first, size());
    if (first == last) {
        return;
    }
    Anchor a = anchor();
    const int count = last - first;
    if (a.place.item >= last) {
        a.place.item -= count;
    } else if (a.place.item >= first) {
        // The anchor's own item goes (the model never cuts out what is being read): the item after the cut takes its line.
        pin_.reset();
        a.place = {first, 0};
    }
    if (pin_) {
        pin_->place.item = a.place.item;
    }
    items_.erase(items_.begin() + first, items_.begin() + last);
    rebuild();
    if (a.place.item >= size()) {
        a.place = {size() - 1, 1};
    }
    restore(a);
}

void StripLayout::clear() {
    items_.clear();
    rebuild();
    top_ = 0;
    pin_.reset();
}

// --- What to load and what to keep on the GPU ---

namespace {

// The strip y range [top, bottom) of the decode window.
struct Span {
    double top = 0;
    double bottom = 0;
};

Span prefetchWindow(const StripLayout& layout, double screensBehind, double screensAhead) {
    const double screen = layout.viewHeight();
    return {layout.top() - std::max(0.0, screensBehind) * screen, layout.bottom() + std::max(0.0, screensAhead) * screen};
}

}  // namespace

std::vector<int> stripPrefetchOrder(const StripLayout& layout, double screensBehind, double screensAhead) {
    std::vector<int> order;
    if (layout.size() == 0) {
        return order;
    }
    const auto [y0, y1] = prefetchWindow(layout, screensBehind, screensAhead);
    const auto [first, last] = layout.overlapping(y0, y1);
    const int middle = std::clamp(layout.middle().item, first, std::max(first, last - 1));
    const auto add = [&](int index) {
        if (index >= first && index < last && layout.item(index).kind == StripItemKind::Page) {
            order.push_back(index);
        }
    };
    add(middle);
    for (int i = middle + 1; i < last; ++i) {
        add(i);
    }
    for (int i = middle - 1; i >= first; --i) {
        add(i);
    }
    return order;
}

StripEnds stripWindowEnds(const StripLayout& layout, double screensBehind, double screensAhead) {
    StripEnds ends;
    if (layout.size() == 0) {
        return ends;
    }
    const auto [y0, y1] = prefetchWindow(layout, screensBehind, screensAhead);
    const auto [first, last] = layout.overlapping(y0, y1);
    ends.top = first == 0 && last > 0;
    ends.bottom = last == layout.size() && last > first;
    return ends;
}

std::vector<StripTile> stripTileWindow(const std::vector<StripPageTiles>& pages, double viewTop, double viewBottom,
                                       int around) {
    // Every row of tiles of these pages, top to bottom, in strip units.
    struct Row {
        double top = 0;
        double bottom = 0;
        int item = -1;
        std::vector<int> tiles;
    };
    std::vector<Row> rows;
    for (const StripPageTiles& page : pages) {
        if (page.decoded.height <= 0 || page.height <= 0) {
            continue;
        }
        const double scale = page.height / page.decoded.height;
        for (std::size_t i = 0; i < page.tiles.size(); ++i) {
            const TileRect& tile = page.tiles[i];
            if (rows.empty() || rows.back().item != page.item ||
                std::fabs(rows.back().top - (page.top + tile.y * scale)) > 1e-6) {
                rows.push_back(Row{page.top + tile.y * scale, page.top + (tile.y + tile.height) * scale, page.item, {}});
            }
            rows.back().tiles.push_back(static_cast<int>(i));
        }
    }
    std::vector<StripTile> kept;
    if (rows.empty()) {
        return kept;
    }
    const int count = static_cast<int>(rows.size());
    const auto onScreen = [&](const Row& row) { return row.top < viewBottom && row.bottom > viewTop; };
    int firstOn = -1;
    int lastOn = -1;
    for (int i = 0; i < count; ++i) {
        if (onScreen(rows[static_cast<std::size_t>(i)])) {
            firstOn = firstOn < 0 ? i : firstOn;
            lastOn = i;
        }
    }
    around = std::max(0, around);
    int lo = 0;
    int hi = 0;  // rows [lo, hi) are kept
    if (firstOn >= 0) {
        lo = firstOn - around;
        hi = lastOn + 1 + around;
    } else {
        // Nothing on screen: `around` rows on each side of where the screen is.
        int below = 0;
        while (below < count && rows[static_cast<std::size_t>(below)].top < viewBottom) {
            ++below;
        }
        lo = below - around;
        hi = below + around;
    }
    lo = std::max(0, lo);
    hi = std::min(count, hi);
    // Upload order: on screen top to bottom, then by the gap to the screen (below first at the same gap).
    std::vector<int> order;
    for (int i = lo; i < hi; ++i) {
        order.push_back(i);
    }
    const auto gap = [&](int i) {
        const Row& row = rows[static_cast<std::size_t>(i)];
        if (onScreen(row)) {
            return 0.0;
        }
        return row.bottom <= viewTop ? viewTop - row.bottom : row.top - viewBottom;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const double ga = gap(a);
        const double gb = gap(b);
        if (ga == 0 && gb == 0) {
            return a < b;  // on screen: top to bottom
        }
        if (ga != gb) {
            return ga < gb;
        }
        return a > b;  // the same gap: the one below first
    });
    for (const int i : order) {
        const Row& row = rows[static_cast<std::size_t>(i)];
        for (const int tile : row.tiles) {
            kept.push_back(StripTile{row.item, tile, row.top, row.bottom, onScreen(row)});
        }
    }
    return kept;
}

// --- Touch, flicks and scrolling ---

namespace {

// The finger's place at time `ms`, between the samples (before the first: the first; after the last: the last).
double placeAtTime(const std::vector<TouchSample>& samples, std::int64_t ms) {
    if (ms <= samples.front().ms) {
        return samples.front().y;
    }
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const TouchSample& a = samples[i - 1];
        const TouchSample& b = samples[i];
        if (ms <= b.ms) {
            if (b.ms == a.ms) {
                return b.y;
            }
            const double t = static_cast<double>(ms - a.ms) / static_cast<double>(b.ms - a.ms);
            return a.y + (b.y - a.y) * t;
        }
    }
    return samples.back().y;
}

}  // namespace

double releaseVelocity(const std::vector<TouchSample>& samples, std::int64_t nowMs, int windowMs) {
    if (samples.size() < 2 || windowMs <= 0) {
        return 0;
    }
    const double end = placeAtTime(samples, nowMs);
    // Rested before lifting: no flick.
    if (std::fabs(end - placeAtTime(samples, nowMs - kFlingRestMs)) < kFlingRestUnits) {
        return 0;
    }
    const std::int64_t from = std::max(nowMs - windowMs, samples.front().ms);
    const std::int64_t span = nowMs - from;
    if (span < 5) {
        return 0;  // too short to tell a speed
    }
    return (end - placeAtTime(samples, from)) * 1000.0 / static_cast<double>(span);
}

double flingStartSpeed(double velocity, double screenHeight) {
    if (!std::isfinite(velocity) || std::fabs(velocity) < kFlingStopSpeed) {
        return 0;
    }
    const double top = std::max(kFlingStopSpeed, kFlingMaxScreensPerSecond * screenHeight);
    return std::clamp(velocity, -top, top);
}

double flingSeconds(double velocity) {
    const double speed = std::fabs(velocity);
    if (speed <= kFlingStopSpeed) {
        return 0;
    }
    return std::log(speed / kFlingStopSpeed) / flingRate();
}

double flingOffset(double velocity, double seconds) {
    const double t = std::clamp(seconds, 0.0, flingSeconds(velocity));
    return velocity / flingRate() * (1 - std::exp(-flingRate() * t));
}

bool flingDone(double velocity, double seconds) {
    return seconds >= flingSeconds(velocity);
}

double stripHoldScroll(double screenHeight, double seconds) {
    return screenHeight * kStripHoldScreensPerSecond * std::max(0.0, seconds);
}

double stripStickScroll(float pushY, double screenHeight, double seconds, float deadZone) {
    const float speed = stickSpeed(std::min(1.0f, std::fabs(pushY)), deadZone);
    const double sign = pushY < 0 ? -1.0 : 1.0;
    return sign * speed * screenHeight / kStripStickCrossSeconds * std::max(0.0, seconds);
}

StripTapArea stripTapArea(float y, float height) {
    if (height <= 0) {
        return StripTapArea::Middle;
    }
    if (y < height * 0.3f) {
        return StripTapArea::Back;
    }
    if (y >= height * 0.7f) {
        return StripTapArea::On;
    }
    return StripTapArea::Middle;
}

// --- Zoom in the strip (WP-13d) ---

double clampStripLeft(double left, double drawnWidth, double screenWidth) {
    if (drawnWidth <= screenWidth) {
        return (screenWidth - drawnWidth) / 2;  // it fits: centred, the sides show the reader's background
    }
    if (!(left <= 0)) {  // also NaN
        return 0;
    }
    return std::max(left, screenWidth - drawnWidth);
}

namespace {

// The strip point under a point of the screen, in strip units (x from the strip's left edge, y from its top), and the view
// that puts strip point (u, v) under screen point `at` at zoom `zoom`.
struct StripPoint {
    double u = 0;
    double v = 0;
};

StripPoint stripPointAt(const StripZoomView& view, PointF at) {
    const double zoom = view.zoom > 0 ? view.zoom : 1;
    return {(at.x - view.left) / zoom, view.top + at.y / zoom};
}

StripZoomView viewFor(StripPoint point, PointF at, float zoom, double stripWidth, double screenWidth) {
    StripZoomView view;
    view.zoom = zoom;
    view.left = clampStripLeft(at.x - point.u * zoom, stripWidth * zoom, screenWidth);
    view.top = point.v - at.y / zoom;
    return view;
}

double fingerDistance(PointF a, PointF b) {
    return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y);
}

}  // namespace

StripZoomView stripZoomAround(StripZoomView from, float to, PointF anchor, double stripWidth, double screenWidth,
                              float maxZoom) {
    return viewFor(stripPointAt(from, anchor), anchor, clampZoom(to, maxZoom), stripWidth, screenWidth);
}

StripZoomView stripPinchView(const StripPinchStart& start, PointF a, PointF b, double stripWidth, double screenWidth,
                             float maxZoom) {
    const double startDistance = fingerDistance(start.a, start.b);
    const float zoom = startDistance >= 1.0
                           ? clampZoom(static_cast<float>(start.view.zoom * fingerDistance(a, b) / startDistance), maxZoom)
                           : clampZoom(start.view.zoom, maxZoom);
    const PointF from{(start.a.x + start.b.x) / 2, (start.a.y + start.b.y) / 2};
    const PointF now{(a.x + b.x) / 2, (a.y + b.y) / 2};
    return viewFor(stripPointAt(start.view, from), now, zoom, stripWidth, screenWidth);
}

bool isStripDoubleTap(PointF first, std::int64_t firstUpMs, PointF second, std::int64_t secondDownMs, float height,
                      float maxDistance, int maxGapMs) {
    const std::int64_t gap = secondDownMs - firstUpMs;
    return gap >= 0 && gap <= maxGapMs && fingerDistance(first, second) <= maxDistance &&
           stripTapArea(first.y, height) == StripTapArea::Middle && stripTapArea(second.y, height) == StripTapArea::Middle;
}

std::vector<int> stripSharpPages(const StripLayout& layout, int count) {
    const double line = layout.top() + layout.viewHeight() / 2;
    std::vector<std::pair<double, int>> pages;  // (distance from the middle line, item)
    const auto [first, last] = layout.overlapping(layout.top(), layout.bottom());
    for (int item = first; item < last; ++item) {
        if (layout.item(item).kind != StripItemKind::Page) {
            continue;
        }
        const double top = layout.itemTop(item);
        const double bottom = layout.itemBottom(item);
        const double distance = line < top ? top - line : line >= bottom ? line - bottom : 0.0;
        pages.emplace_back(distance, item);
    }
    // Nearest first; at the same distance the lower one (a bigger item index), since reading goes down.
    std::stable_sort(pages.begin(), pages.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : a.second > b.second;
    });
    std::vector<int> items;
    for (std::size_t i = 0; i < pages.size() && static_cast<int>(i) < count; ++i) {
        items.push_back(pages[i].second);
    }
    return items;
}

// --- The spot on the server ---

std::string stripSpotValue(int page, double share) {
    if (!std::isfinite(share)) {
        share = 0;
    }
    const long tenThousandths = std::lround(std::clamp(share, 0.0, 1.0) * 10000.0);
    std::string fraction = std::to_string(tenThousandths % 10000);
    fraction.insert(0, 4 - fraction.size(), '0');
    return std::to_string(std::max(0, page)) + ":" + std::to_string(tenThousandths / 10000) + "." + fraction;
}

std::optional<StripSpot> parseStripSpot(std::string_view text) {
    if (text.empty() || text.size() > kStripSpotMaxChars) {
        return std::nullopt;
    }
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) {
        return std::nullopt;
    }
    const char* begin = text.data();
    const char* split = begin + colon;
    const char* end = begin + text.size();
    StripSpot spot;
    const auto pageResult = std::from_chars(begin, split, spot.page);
    if (pageResult.ec != std::errc() || pageResult.ptr != split || spot.page < 0) {
        return std::nullopt;
    }
    double share = 0;
    const auto shareResult = std::from_chars(split + 1, end, share);
    if (shareResult.ec != std::errc() || shareResult.ptr != end || !std::isfinite(share)) {
        return std::nullopt;
    }
    spot.share = std::clamp(share, 0.0, 1.0);
    return spot;
}

// --- "By itself" ---

namespace {

std::string trimmedLower(std::string_view text) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

}  // namespace

bool looksLikeStrip(const std::vector<std::string>& genres) {
    for (const std::string& genre : genres) {
        const std::string tag = trimmedLower(genre);
        if (tag == "long strip" || tag == "webtoon" || tag == "webtoons") {
            return true;
        }
    }
    return false;
}

bool isStripPage(ImageSize source) {
    return source.width > 0 && source.height > kStripPageShape * source.width;
}

}  // namespace snx::core
