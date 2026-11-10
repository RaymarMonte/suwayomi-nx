#include "core/ReaderMath.hpp"

#include <algorithm>
#include <cmath>

namespace snx::core {

namespace {

constexpr float kTapSideShare = 0.3f;   // each side's tap zone, as a share of the width
constexpr float kSwipeSideways = 1.5f;  // a swipe must go this many times further sideways than up or down
constexpr float kSpreadAspect = 1.2f;
constexpr float kEdgeSlack = 0.5f;      // less than this beyond the edge counts as "at the edge" (rounding)

// The allowed range for one axis of the corner: centred if the page fits, else from (screen - page) to 0.
float clampAxis(float wanted, float page, float screen) {
    if (page <= screen) {
        return (screen - page) / 2;
    }
    return std::clamp(wanted, screen - page, 0.0f);
}

}  // namespace

int pageStep(Side side, ReadingDirection direction) {
    const bool next = direction == ReadingDirection::RightToLeft ? side == Side::Left : side == Side::Right;
    return next ? 1 : -1;
}

TapArea tapArea(float x, float width) {
    if (width <= 0) {
        return TapArea::Middle;
    }
    if (x < width * kTapSideShare) {
        return TapArea::Left;
    }
    if (x >= width * (1 - kTapSideShare)) {
        return TapArea::Right;
    }
    return TapArea::Middle;
}

std::optional<Side> swipeSide(float dx, float dy, float minDistance) {
    if (std::fabs(dx) < minDistance || std::fabs(dx) <= kSwipeSideways * std::fabs(dy)) {
        return std::nullopt;
    }
    return dx < 0 ? Side::Right : Side::Left;
}

bool isTap(float dx, float dy, int heldMs, float maxSlide, int maxHeldMs) {
    return heldMs <= maxHeldMs && dx * dx + dy * dy <= maxSlide * maxSlide;
}

SizeF fitPage(ImageSize image, SizeF screen, PageFit fit) {
    if (image.width <= 0 || image.height <= 0 || screen.width <= 0 || screen.height <= 0) {
        return {};
    }
    const float w = static_cast<float>(image.width);
    const float h = static_cast<float>(image.height);
    float scale = 1;
    switch (fit) {
        case PageFit::Screen:
            scale = std::min(screen.width / w, screen.height / h);
            break;
        case PageFit::Width:
            scale = screen.width / w;
            break;
        case PageFit::Height:
            scale = screen.height / h;
            break;
        case PageFit::Original:
            break;
    }
    return {w * scale, h * scale};
}

ImageSize decodeBoxFor(PageFit fit, ImageSize screen) {
    switch (fit) {
        case PageFit::Screen:
            return screen;
        case PageFit::Width:
            return {screen.width, 0};
        case PageFit::Height:
            return {0, screen.height};
        case PageFit::Original:
            break;
    }
    return {0, 0};
}

bool isSpread(ImageSize image) {
    return image.width > 0 && image.height > 0 &&
           static_cast<float>(image.width) > kSpreadAspect * static_cast<float>(image.height);
}

PointF clampPosition(PointF wanted, SizeF page, SizeF screen) {
    return {clampAxis(wanted.x, page.width, screen.width), clampAxis(wanted.y, page.height, screen.height)};
}

PointF startPosition(SizeF page, SizeF screen, ReadingDirection direction) {
    // The right edge for right to left: the corner as far left as it goes. The top: the corner as low as it goes (0).
    const float x = direction == ReadingDirection::RightToLeft ? screen.width - page.width : 0.0f;
    return clampPosition({x, 0}, page, screen);
}

bool canMoveToward(PointF position, SizeF page, SizeF screen, Side side) {
    if (page.width <= screen.width) {
        return false;
    }
    if (side == Side::Left) {
        return position.x < -kEdgeSlack;  // part of the page is left of the screen
    }
    return position.x + page.width > screen.width + kEdgeSlack;
}

// --- Zoom and pan (WP-13) ---

namespace {

constexpr float kZoomSteps[] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
constexpr float kZoomSlack = 0.001f;    // zooms closer than this count as the same
constexpr float kSnapToFit = 1.05f;
constexpr float kArrowShare = 0.75f;    // one D-pad press moves a zoomed page three quarters of the screen

float distance(PointF a, PointF b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

PointF middle(PointF a, PointF b) {
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}

SizeF scaled(SizeF size, float zoom) {
    return {size.width * zoom, size.height * zoom};
}

// The corner that puts the page point at (u, v) (shares of the page's size) under `at`, for a page of size `page`.
PointF cornerFor(float u, float v, PointF at, SizeF page) {
    return {at.x - u * page.width, at.y - v * page.height};
}

}  // namespace

float clampZoom(float zoom, float maxZoom) {
    return std::clamp(zoom, 1.0f, std::max(1.0f, maxZoom));
}

float zoomStep(float zoom, int direction, float maxZoom) {
    const float top = std::max(1.0f, maxZoom);
    const float now = clampZoom(zoom, top);
    if (direction > 0) {
        for (const float step : kZoomSteps) {
            if (step > now + kZoomSlack) {
                return std::min(step, top);
            }
        }
        return top;
    }
    if (direction < 0) {
        float below = 1.0f;
        for (const float step : kZoomSteps) {
            if (step < now - kZoomSlack && step <= top) {
                below = step;
            }
        }
        return below;
    }
    return now;
}

float zoomBucket(float zoom, float maxZoom) {
    const float top = std::max(1.0f, maxZoom);
    for (const float step : kZoomSteps) {
        if (step >= zoom - kZoomSlack) {
            return std::min(step, top);
        }
    }
    return top;
}

bool snapsToFit(float zoom) {
    return zoom < kSnapToFit;
}

bool isZoomed(float zoom) {
    return zoom > 1.0f + kZoomSlack;
}

PointF zoomAround(PointF position, SizeF fit, float from, float to, PointF anchor, SizeF screen) {
    const SizeF before = scaled(fit, from);
    const SizeF after = scaled(fit, to);
    if (before.width <= 0 || before.height <= 0) {
        return clampPosition(position, after, screen);
    }
    const float u = (anchor.x - position.x) / before.width;
    const float v = (anchor.y - position.y) / before.height;
    return clampPosition(cornerFor(u, v, anchor, after), after, screen);
}

ZoomView pinchView(const PinchStart& start, PointF a, PointF b, SizeF fit, SizeF screen, float maxZoom) {
    const float startDistance = distance(start.a, start.b);
    const float zoom = startDistance >= 1.0f ? clampZoom(start.zoom * distance(a, b) / startDistance, maxZoom)
                                             : clampZoom(start.zoom, maxZoom);
    const SizeF before = scaled(fit, start.zoom);
    const SizeF after = scaled(fit, zoom);
    if (before.width <= 0 || before.height <= 0) {
        return {zoom, clampPosition(start.position, after, screen)};
    }
    // The page point that was between the fingers at the start stays between them.
    const PointF from = middle(start.a, start.b);
    const float u = (from.x - start.position.x) / before.width;
    const float v = (from.y - start.position.y) / before.height;
    return {zoom, clampPosition(cornerFor(u, v, middle(a, b), after), after, screen)};
}

bool isDoubleTap(PointF first, std::int64_t firstUpMs, PointF second, std::int64_t secondDownMs, float width,
                 float maxDistance, int maxGapMs) {
    const std::int64_t gap = secondDownMs - firstUpMs;
    return gap >= 0 && gap <= maxGapMs && distance(first, second) <= maxDistance &&
           tapArea(first.x, width) == TapArea::Middle && tapArea(second.x, width) == TapArea::Middle;
}

bool canMoveToward(PointF position, SizeF page, SizeF screen, Arrow arrow) {
    switch (arrow) {
        case Arrow::Left:
            return canMoveToward(position, page, screen, Side::Left);
        case Arrow::Right:
            return canMoveToward(position, page, screen, Side::Right);
        case Arrow::Up:
            return page.height > screen.height && position.y < -kEdgeSlack;
        case Arrow::Down:
            return page.height > screen.height && position.y + page.height > screen.height + kEdgeSlack;
    }
    return false;
}

ArrowEffect arrowEffect(Arrow arrow, bool zoomed, bool canMove, bool fresh) {
    const bool sideways = arrow == Arrow::Left || arrow == Arrow::Right;
    if (!zoomed) {
        if (!sideways) {
            return ArrowEffect::Move;
        }
        return fresh ? ArrowEffect::Turn : ArrowEffect::Nothing;
    }
    if (canMove) {
        return ArrowEffect::Move;
    }
    return sideways && fresh ? ArrowEffect::Turn : ArrowEffect::Nothing;
}

std::optional<Side> arrowSide(Arrow arrow) {
    if (arrow == Arrow::Left) {
        return Side::Left;
    }
    if (arrow == Arrow::Right) {
        return Side::Right;
    }
    return std::nullopt;
}

PointF arrowMove(Arrow arrow, SizeF screen) {
    // The page's corner moves against the arrow: right shows more of the page's right side.
    switch (arrow) {
        case Arrow::Left:
            return {kArrowShare * screen.width, 0};
        case Arrow::Right:
            return {-kArrowShare * screen.width, 0};
        case Arrow::Up:
            return {0, kArrowShare * screen.height};
        case Arrow::Down:
            return {0, -kArrowShare * screen.height};
    }
    return {};
}

float glideProgress(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const float rest = 1 - clamped;
    return 1 - rest * rest * rest;
}

PointF readerStick(ReaderOrientation orientation, PointF stick) {
    return screenMoveToReader(orientation, stick);
}

float stickSpeed(float amount, float deadZone) {
    const float a = std::fabs(amount);
    if (a <= deadZone || deadZone >= 1) {
        return 0;
    }
    const float share = std::min(1.0f, (a - deadZone) / (1 - deadZone));
    return share * share;
}

PointF stickMove(PointF stick, SizeF screen, float seconds, float crossSeconds, float deadZone) {
    const float length = std::hypot(stick.x, stick.y);
    if (length <= deadZone || crossSeconds <= 0) {
        return {};
    }
    const float speed = stickSpeed(std::min(length, 1.0f), deadZone);
    const float dx = stick.x / length;
    const float dy = stick.y / length;
    return {-dx * speed * screen.width / crossSeconds * seconds, -dy * speed * screen.height / crossSeconds * seconds};
}

std::optional<Arrow> stickArrow(PointF stick, float threshold) {
    const float ax = std::fabs(stick.x);
    const float ay = std::fabs(stick.y);
    if (std::max(ax, ay) < threshold) {
        return std::nullopt;
    }
    if (ax >= ay) {
        return stick.x > 0 ? Arrow::Right : Arrow::Left;
    }
    return stick.y > 0 ? Arrow::Down : Arrow::Up;
}

bool splitsSpread(SpreadSplit mode, ReaderOrientation orientation, bool spread) {
    switch (mode) {
        case SpreadSplit::Portrait:
            return spread && isPortrait(orientation);
        case SpreadSplit::Always:
            return spread;
        case SpreadSplit::Never:
            break;
    }
    return false;
}

Side spreadHalfSide(int half, ReadingDirection direction) {
    const Side first = direction == ReadingDirection::RightToLeft ? Side::Right : Side::Left;
    const Side second = first == Side::Right ? Side::Left : Side::Right;
    return half == 0 ? first : second;
}

TileRect spreadHalf(ImageSize image, Side side) {
    if (image.width <= 0 || image.height <= 0) {
        return {};
    }
    const int left = image.width / 2;  // the narrower one when the width is odd
    if (side == Side::Left) {
        return {0, 0, left, image.height};
    }
    return {left, 0, image.width - left, image.height};
}

ImageSize spreadHalfSize(ImageSize image, Side side) {
    const TileRect half = spreadHalf(image, side);
    return {half.width, half.height};
}

ImageSize splitDecodeBox(ImageSize box) {
    return {box.width > 0 ? box.width * 2 : 0, box.height};
}

ImageSize zoomDecodeBox(PageFit fit, ImageSize screen, float zoom, float maxZoom, float maxDecodeScale, bool split) {
    const ImageSize base = split ? splitDecodeBox(decodeBoxFor(fit, screen)) : decodeBoxFor(fit, screen);
    const float z = zoomBucket(zoom, maxZoom);
    const auto side = [z, maxDecodeScale](int fitSide, int screenSide) {
        if (fitSide <= 0) {
            return 0;  // no limit that way (fit width's height, fit height's width)
        }
        const int cap = std::max(1, static_cast<int>(std::lround(static_cast<float>(screenSide) * maxDecodeScale)));
        const int wanted = std::max(1, static_cast<int>(std::lround(static_cast<float>(fitSide) * z)));
        return std::min(wanted, std::max(cap, fitSide));  // never below the fit's own box
    };
    return {side(base.width, screen.width), side(base.height, screen.height)};
}

bool isSharp(ImageSize decoded, ImageSize source, ImageSize box) {
    const ImageSize wanted = fitInside(source, box);
    return decoded.width + 1 >= wanted.width && decoded.height + 1 >= wanted.height;
}

std::vector<int> prefetchOrder(int current, int count, int behind, int ahead) {
    std::vector<int> order;
    const auto add = [&](int page) {
        if (page >= 0 && page < count) {
            order.push_back(page);
        }
    };
    add(current);
    for (int i = 1; i <= ahead; ++i) {
        add(current + i);
    }
    for (int i = 1; i <= behind; ++i) {
        add(current - i);
    }
    return order;
}

bool nearChapterEnd(int current, int count, int ahead) {
    return ahead > 0 && count > 0 && current + ahead >= count;
}

ReaderOrientation effectiveOrientation(ReaderOrientation saved, bool handheld) {
    return handheld ? saved : ReaderOrientation::Landscape;
}

bool isPortrait(ReaderOrientation orientation) {
    return orientation != ReaderOrientation::Landscape;
}

ReaderOrientation quickRotate(ReaderOrientation current, ReaderOrientation lastPortrait) {
    if (isPortrait(current)) {
        return ReaderOrientation::Landscape;
    }
    return isPortrait(lastPortrait) ? lastPortrait : ReaderOrientation::PortraitLeft;
}

SizeF readerScreen(ReaderOrientation orientation, SizeF screen) {
    return isPortrait(orientation) ? SizeF{screen.height, screen.width} : screen;
}

ImageSize readerScreen(ReaderOrientation orientation, ImageSize screen) {
    return isPortrait(orientation) ? ImageSize{screen.height, screen.width} : screen;
}

Transform readerTransform(ReaderOrientation orientation, SizeF screen) {
    switch (orientation) {
        case ReaderOrientation::PortraitLeft:
            // The reader's x runs down the real screen, its y from the real right edge to the left: (W - y, x).
            return {0, 1, -1, 0, screen.width, 0};
        case ReaderOrientation::PortraitRight:
            // The reader's x runs up the real screen, its y from the real left edge to the right: (y, H - x).
            return {0, -1, 1, 0, 0, screen.height};
        case ReaderOrientation::Landscape:
            break;
    }
    return {};
}

PointF readerToScreen(ReaderOrientation orientation, PointF reader, SizeF screen) {
    const Transform t = readerTransform(orientation, screen);
    return {t.a * reader.x + t.c * reader.y + t.e, t.b * reader.x + t.d * reader.y + t.f};
}

PointF screenToReader(ReaderOrientation orientation, PointF point, SizeF screen) {
    switch (orientation) {
        case ReaderOrientation::PortraitLeft:
            return {point.y, screen.width - point.x};
        case ReaderOrientation::PortraitRight:
            return {screen.height - point.y, point.x};
        case ReaderOrientation::Landscape:
            break;
    }
    return point;
}

PointF screenMoveToReader(ReaderOrientation orientation, PointF move) {
    switch (orientation) {
        case ReaderOrientation::PortraitLeft:
            return {move.y, -move.x};
        case ReaderOrientation::PortraitRight:
            return {-move.y, move.x};
        case ReaderOrientation::Landscape:
            break;
    }
    return move;
}

Arrow readerArrow(ReaderOrientation orientation, Arrow pressed) {
    // The press as a movement on the real screen, turned like a swipe.
    PointF move;
    switch (pressed) {
        case Arrow::Up:
            move = {0, -1};
            break;
        case Arrow::Down:
            move = {0, 1};
            break;
        case Arrow::Left:
            move = {-1, 0};
            break;
        case Arrow::Right:
            move = {1, 0};
            break;
    }
    const PointF turned = screenMoveToReader(orientation, move);
    if (turned.x != 0) {
        return turned.x > 0 ? Arrow::Right : Arrow::Left;
    }
    return turned.y > 0 ? Arrow::Down : Arrow::Up;
}

}  // namespace snx::core
