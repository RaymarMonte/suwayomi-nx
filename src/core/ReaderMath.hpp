#pragma once

#include <cstdint>
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

// Whether a touch that borealis took for a drag was a tap after all. borealis starts a drag once the finger has moved 6 units,
// so a quick tap that slides a little would do nothing. It is a tap when the finger moved at most maxSlide units in all
// (dx, dy: from where it touched down to where it let go) and let go within maxHeldMs.
bool isTap(float dx, float dy, int heldMs, float maxSlide, int maxHeldMs);

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

// A double-page spread: more than 1.2 times as wide as tall (ARCHITECTURE.md "core" reader math). It may show as two halves
// (splitsSpread, WP-13c).
bool isSpread(ImageSize image);

// Where a drawn page's top-left corner may be (screen units): a page smaller than the screen in a direction is centred that
// way; a bigger one may move, but never so far that a gap shows at one of its edges.
PointF clampPosition(PointF wanted, SizeF page, SizeF screen);

// Where a page starts when it comes on screen: at its top, and sideways at the reading direction's start (the left edge for
// left to right, the right edge for right to left). Centred where it fits.
PointF startPosition(SizeF page, SizeF screen, ReadingDirection direction);

// Whether more of the page lies beyond the screen's `side` edge (so a swipe that way moves the page instead of turning it).
bool canMoveToward(PointF position, SizeF page, SizeF screen, Side side);

// --- Holding the console sideways (WP-22) ---
// In portrait the reader draws everything turned a quarter turn, on a "reader screen" with width and height swapped (720 x 1280
// units in handheld). The rules above work on that screen; these functions turn points, moves and D-pad presses between the
// real (landscape) screen and the reader's. `screen` below is always the real screen, e.g. 1280 x 720.

// The orientation in effect: the saved choice in handheld mode, always landscape when docked (on the TV).
ReaderOrientation effectiveOrientation(ReaderOrientation saved, bool handheld);
// What the quick rotate (Y, the menu's Rotate) goes to: landscape <-> the portrait way used last (`lastPortrait`; turned left
// if that isn't a portrait way).
ReaderOrientation quickRotate(ReaderOrientation current, ReaderOrientation lastPortrait);
bool isPortrait(ReaderOrientation orientation);

// The reader's screen: `screen` as it is for landscape, width and height swapped for portrait.
SizeF readerScreen(ReaderOrientation orientation, SizeF screen);
ImageSize readerScreen(ReaderOrientation orientation, ImageSize screen);  // the same in real pixels (decodeBoxFor)

// A 2D transform: real-screen point = (a * x + c * y + e, b * x + d * y + f) for a reader point (x, y). The same six numbers
// as NanoVG's nvgTransform, so the drawing and the touch mapping come from one place.
struct Transform {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};
// Turned left: the reader's top is the real screen's right edge. Turned right: its top is the real left edge.
Transform readerTransform(ReaderOrientation orientation, SizeF screen);
PointF readerToScreen(ReaderOrientation orientation, PointF reader, SizeF screen);
PointF screenToReader(ReaderOrientation orientation, PointF point, SizeF screen);
// A movement (a swipe or drag: where the finger went minus where it started), real screen -> reader.
PointF screenMoveToReader(ReaderOrientation orientation, PointF move);

// A D-pad or stick direction.
enum class Arrow { Up, Down, Left, Right };
// What a press means on the reader's screen: the one that points to the screen's right as the console is held counts as
// right. Turned left, the D-pad's right points up; turned right, it points down.
Arrow readerArrow(ReaderOrientation orientation, Arrow pressed);

// --- Zoom and pan (WP-13) ---
// Zoom 1 is the fit (fitPage); a page zoomed by z is drawn z times the fit's size. All points and sizes below are on the
// reader's screen (units), like the rules above.

// The zoom kept between the fit (1) and maxZoom (RuntimeProfile::maxZoom).
float clampZoom(float zoom, float maxZoom);
// ZR / ZL: the next step of 1, 1.5, 2, 3, 4 above (`direction` +1) or below (-1) `zoom`, capped at maxZoom (a cap between
// two steps is a step of its own). Already at the end: the same zoom.
float zoomStep(float zoom, int direction, float maxZoom);
// The step a zoom decodes for: the smallest step at or above it (1.2 -> 1.5, 1.7 -> 2), at most maxZoom. A pinch to 1.7
// decodes for 2, so small changes don't decode the page again.
float zoomBucket(float zoom, float maxZoom);
// A pinch let go below this (1.05) goes back to the fit.
bool snapsToFit(float zoom);
// Whether the page is zoomed in at all (a hair above 1 counts as the fit).
bool isZoomed(float zoom);

// Where the page's corner goes when its zoom changes from `from` to `to` (page = the fit's size): the point of the page under
// `anchor` (a point on the screen) stays under it, then clampPosition (no gap at an edge, centred where it fits).
PointF zoomAround(PointF position, SizeF fit, float from, float to, PointF anchor, SizeF screen);

// A two-finger pinch. At its start: the zoom, the page's corner and the two fingers. The zoom follows the fingers' distance
// (against the distance at the start), kept between 1 and maxZoom; the point of the page that was between the fingers at the
// start stays between them as they move (it pans with them). Fingers that start at one point give the start's zoom.
struct PinchStart {
    float zoom = 1;
    PointF position;
    PointF a;
    PointF b;
};
struct ZoomView {
    float zoom = 1;
    PointF position;
};
ZoomView pinchView(const PinchStart& start, PointF a, PointF b, SizeF fit, SizeF screen, float maxZoom);

// A double tap (WP-13): the second touch went down within maxGapMs (250) of the first tap's lift, within maxDistance (40)
// units of it, and both lie in the middle zone (tapArea). Either tap may slide a little (isTap decides what a tap is).
bool isDoubleTap(PointF first, std::int64_t firstUpMs, PointF second, std::int64_t secondDownMs, float width,
                 float maxDistance = 40, int maxGapMs = 250);

// Whether more of the page lies beyond the screen's edge toward `arrow` (up: above the screen's top edge, ...).
bool canMoveToward(PointF position, SizeF page, SizeF screen, Arrow arrow);

// What a D-pad press toward `arrow` (on the reader's screen) does. Not zoomed (as before WP-13): left/right turn the page,
// up/down move a tall page. Zoomed: the view moves that way while more of the page lies there; at the page's edge a press
// toward a side turns, up/down do nothing. A turn needs a fresh press: holding a button never turns more than one page
// (D46's leftover), a held one only moves.
enum class ArrowEffect { Nothing, Turn, Move };
ArrowEffect arrowEffect(Arrow arrow, bool zoomed, bool canMove, bool fresh);
// The side a left/right arrow turns toward (up and down turn nothing: nullopt).
std::optional<Side> arrowSide(Arrow arrow);
// How far one D-pad press moves a zoomed page (a share of the screen: 0.75) and how long that glide takes (0.15 s).
PointF arrowMove(Arrow arrow, SizeF screen);
inline constexpr int kArrowGlideMs = 150;
// Where a glide is after `t` of its time (0..1): eases out, so it starts fast and settles.
float glideProgress(float t);

// A stick (WP-13): x to the right, y down, each -1..1, as the console is held. readerStick turns it with the screen like
// readerArrow: on the reader's screen, the push that points to its right has x > 0.
PointF readerStick(ReaderOrientation orientation, PointF stick);
// How fast a push moves the page, 0..1: nothing inside the dead zone (0.15), then faster the further pushed (a curve, so a
// small push is fine control), 1 at a full push.
float stickSpeed(float amount, float deadZone = 0.15f);
// How far the page moves in `seconds` for a push `stick` (on the reader's screen): a full push crosses one screen in
// crossSeconds (0.6). The page moves against the push (push right: see more on the right).
PointF stickMove(PointF stick, SizeF screen, float seconds, float crossSeconds = 0.6f, float deadZone = 0.15f);
// The arrow a stick push points to, once it is past `threshold` (0.5: "half way", the D-pad press a push counts as); the
// larger of the two directions wins. Nothing below.
std::optional<Arrow> stickArrow(PointF stick, float threshold = 0.5f);

// --- Split spreads (WP-13c) ---
// A split spread shows as two halves, one after the other, each drawn like a page of its own (fit, zoom and pan treat the
// half as the page). Both halves are the same page for the server (progress, the page number, the prefetch window).

// Whether a page shows as two halves: it is a spread (isSpread) and the setting asks for it in this orientation (the one in
// effect: docked is landscape). Portrait: only in portrait. Always: in landscape too. Never: never.
bool splitsSpread(SpreadSplit mode, ReaderOrientation orientation, bool spread);
// Which side of the image half `half` is: 0 = the first in reading order, 1 = the second. Right to left starts with the
// right half; left to right and vertical with the left half.
Side spreadHalfSide(int half, ReadingDirection direction);
// The half of an image of `image`'s size on `side`, in that image's pixels: the left half is the narrower one when the width
// is odd (1441 -> 720 + 721), so the two always add up to the whole. Works for the source size (for the fit) and for the
// decoded size (what the view draws).
TileRect spreadHalf(ImageSize image, Side side);
ImageSize spreadHalfSize(ImageSize image, Side side);  // spreadHalf's width and height
// The box to decode a split spread at: twice as wide as `box` (each half gets the pixels a page would; a width of 0, no
// limit, stays 0). Fit height has no width limit, so its box stays the same.
ImageSize splitDecodeBox(ImageSize box);

// The box to decode the page on screen at for `zoom` (WP-13): the fit's box (decodeBoxFor; splitDecodeBox for a split
// spread, WP-13c) times zoomBucket(zoom), each limited side at most maxDecodeScale times the reader's screen as it is held
// (portrait: 2 x 720 wide, 2 x 1280 tall), never below the fit's box. `screen` = the reader's screen in real pixels. The
// loader still keeps it within the source size and maxDecodeBytes.
ImageSize zoomDecodeBox(PageFit fit, ImageSize screen, float zoom, float maxZoom, float maxDecodeScale,
                        bool split = false);
// Whether a decoded image has the detail `box` asks for (as far as the source has it): at least as wide and as tall as the
// source fitted inside the box (fitInside), give or take a pixel of rounding.
bool isSharp(ImageSize decoded, ImageSize source, ImageSize box);

// The pages to load around `current` (0 <= current < count), most urgent first: the current page, then the next `ahead` pages
// nearest first, then the previous `behind` pages nearest first. Pages outside [0, count) are left out; a current page
// outside it gives only the pages of the window that exist.
std::vector<int> prefetchOrder(int current, int count, int behind, int ahead);

// Whether the window of `ahead` pages after `current` reaches past the chapter's last page, so the next chapter's first pages
// are wanted (RuntimeProfile::nextChapterPrefetchPages). Never with ahead 0 (prefetch off).
bool nearChapterEnd(int current, int count, int ahead);

}  // namespace snx::core
