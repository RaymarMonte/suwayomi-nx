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

}  // namespace snx::core
