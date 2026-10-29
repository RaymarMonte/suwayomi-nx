#pragma once

#include <optional>
#include <vector>

#include "core/AppConfig.hpp"
#include "core/ImageGeometry.hpp"

// The paged reader's math (ARCHITECTURE.md "core" reader math, WP-11): which way a key, tap or swipe turns, how big a page is
// drawn for each fit, where it sits on the screen and how far it can be moved, and which pages to load around the current one.
// Pure functions, so the reader's rules are unit-tested without a screen.
namespace snx::core {

// A side of the screen (a key, a tap zone, or where a swipe goes).
enum class Side { Left, Right };

// The page a press or tap toward `side` goes to: +1 = the next page, -1 = the previous one. Left to right: right is next.
// Right to left: left is next (everything mirrored). Vertical (the vertical reader is WP-13) pages like left to right.
int pageStep(Side side, ReadingDirection direction);

// Where a tap lands (D16): the outer 30 % on each side turn pages, the middle opens the menu.
enum class TapArea { Left, Middle, Right };
TapArea tapArea(float x, float width);

// The way a finished swipe turns, if it turns at all: it went at least minDistance sideways, and clearly more sideways than up
// or down. The finger moving to the left shows what is on the right (Side::Right), like pushing the page away; dx > 0 = the
// finger moved right. Nothing for a short or mostly vertical move (that one scrolls the page).
std::optional<Side> swipeSide(float dx, float dy, float minDistance);

// A size or a point in screen units (borealis's 1280 x 720 coordinates).
struct SizeF {
    float width = 0;
    float height = 0;
};
struct PointF {
    float x = 0;
    float y = 0;
};

// How big a page of `image` (its size as stored) is drawn on `screen`. Screen: all of it fits. Width: as wide as the screen
// (a tall page is then taller than the screen). Height: as tall as the screen (a very wide page is then wider). Original: the
// image's own pixels, 1 pixel = 1 screen unit. An empty image or screen gives {0, 0}.
SizeF fitPage(ImageSize image, SizeF screen, PageFit fit);

// The box to decode a page at for a fit, in real pixels (`screen` = the screen's real pixels), so the decoded image is about
// the size it is drawn at: Screen: the screen; Width: {screen width, no limit}; Height: {no limit, screen height};
// Original: no limit (the image loader caps every box at maxDecodeScale times the screen anyway).
ImageSize decodeBoxFor(PageFit fit, ImageSize screen);

// A double-page spread: more than 1.2 times as wide as tall (ARCHITECTURE.md "core" reader math). Shown whole like any page;
// the reader only reports it (a later WP may split it).
bool isSpread(ImageSize image);

// Where a drawn page's top-left corner may be (screen units): a page smaller than the screen in a direction is centred that
// way; a bigger one may move, but never so far that a gap shows at one of its edges.
PointF clampPosition(PointF wanted, SizeF page, SizeF screen);

// Where a page starts when it comes on screen: at its top, and sideways at the reading direction's start (the left edge for
// left to right, the right edge for right to left). Centred where it fits.
PointF startPosition(SizeF page, SizeF screen, ReadingDirection direction);

// Whether more of the page lies beyond the screen's `side` edge (so a swipe that way moves the page instead of turning it).
bool canMoveToward(PointF position, SizeF page, SizeF screen, Side side);

// The pages to load around `current` (0 <= current < count), most urgent first: the current page, then the next `ahead` pages
// nearest first, then the previous `behind` pages nearest first. Pages outside [0, count) are left out; a current page
// outside it gives only the pages of the window that exist.
std::vector<int> prefetchOrder(int current, int count, int behind, int ahead);

// Whether the window of `ahead` pages after `current` reaches past the chapter's last page, so the next chapter's first pages
// are wanted (RuntimeProfile::nextChapterPrefetchPages). Never with ahead 0 (prefetch off).
bool nearChapterEnd(int current, int count, int ahead);

}  // namespace snx::core
