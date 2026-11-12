#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/AppConfig.hpp"
#include "core/ImageGeometry.hpp"
#include "core/ReaderMath.hpp"

// The webtoon reader's math (WP-13b, ARCHITECTURE.md "core" reader math): the vertical strip that joins a chapter's pages
// (and the neighbouring chapters, with a band between them) into one long picture you scroll. Pure functions and one small
// class (StripLayout), so the strip's rules are unit-tested without a screen (tests/unit/test_core_strip.cpp).
//
// Units: everything in the strip is in "strip units" at zoom 1: a page drawn at the strip's width (stripWidth) is
// stripPageHeight tall. The reader's screen shows `viewHeight` = its height / the zoom of them. Strip y grows downward from
// the top of the first item; `top` is the strip y at the reader screen's top edge. Three long chapters are 10^5 .. 10^6 units,
// so the strip uses double.
namespace snx::core {

// --- Width and decode boxes ---

// The strip's width on the reader's screen (units, or real pixels: the same rule): in landscape the setting's share of the
// width (half, three quarters, all of it), in portrait always the whole width. Docked is landscape (effectiveOrientation).
float stripWidth(StripWidth setting, ReaderOrientation orientation, float readerScreenWidth);
// The box strip pages decode at: {the strip's width in real pixels, no limit}. `readerScreen` = the reader's screen in real
// pixels (portrait: 720 x 1280). The loader keeps its 15 % slack (D39): an 800-wide strip stays 800 in portrait (720), and a
// WebP decodes straight to 640 in landscape.
ImageSize stripDecodeBox(StripWidth setting, ReaderOrientation orientation, ImageSize readerScreen);
// Zoomed (zoom > 1): the strip's width x the zoom's step, as zoomDecodeBox does for fit Width (capped at maxDecodeScale x the
// strip's width; never below the plain box).
ImageSize stripZoomDecodeBox(StripWidth setting, ReaderOrientation orientation, ImageSize readerScreen, float zoom,
                             float maxZoom, float maxDecodeScale);

// A page's height in the strip: its shape (the source size) at the strip's width. At least 1 unit.
double stripPageHeight(ImageSize source, double stripWidth);
// A page whose size isn't known yet: as tall as the chapter's last known page shape would make it, else 1.5 x the width.
inline constexpr double kUnknownPageShape = 1.5;
double estimatedPageHeight(std::optional<ImageSize> lastKnownShape, double stripWidth);
// The band between two chapters (and above the first, below the last): a third of the reader's screen.
double stripBandHeight(double readerScreenHeight);

// --- The layout ---

enum class StripItemKind { Band, Page };

struct StripItem {
    StripItemKind kind = StripItemKind::Page;
    int chapterId = 0;  // Page: its chapter. Band: 0 (the model knows its neighbours from the items above and below)
    int page = -1;      // Page: its index in the chapter
    double height = 0;  // strip units
};

// A place in the strip: an item and how far down it (0 = its top, 1 = its bottom).
struct StripPlace {
    int item = -1;  // -1: none (an empty strip)
    double share = 0;
};

// The strip as a list of items top to bottom, and where the screen is on it. Every change keeps the anchor still: by default
// the place under the screen's middle line; pin() holds another place at another line instead (opening at a page's top)
// until a scroll. So a change above the anchor moves `top` by as much as the height changed, a change below it moves
// nothing, and a change of the anchor's own item keeps its share under the line. A page arriving above the screen never
// moves what you see.
class StripLayout {
  public:
    // The screen's height in strip units (the reader screen's height / the zoom). The anchor stays put.
    void setViewHeight(double height);
    double viewHeight() const { return viewHeight_; }

    const std::vector<StripItem>& items() const { return items_; }
    int size() const { return static_cast<int>(items_.size()); }
    const StripItem& item(int index) const;  // a wrong index gives an empty band
    double contentHeight() const { return tops_.back(); }
    double itemTop(int index) const;     // 0 for a wrong index
    double itemBottom(int index) const;  // itemTop + height
    // The item at strip y (an edge belongs to the item below it); y outside the strip: the first or last item. -1 if empty.
    int itemAt(double y) const;
    // The items that overlap [y0, y1): {first, last} with last one past the end (first == last: none).
    std::pair<int, int> overlapping(double y0, double y1) const;
    // The item of page `page` of chapter `chapterId`; -1 if it isn't in the strip.
    int find(int chapterId, int page) const;

    // Scrolling: top = the strip's y at the screen's top edge.
    double top() const { return top_; }
    double bottom() const { return top_ + viewHeight_; }
    // `top` kept within the strip: no gap above the first item or below the last; a strip shorter than the screen sits at
    // the top (0).
    double clampTop(double top) const;
    double maxTop() const { return clampTop(contentHeight()); }
    // Moves the screen to `top` (clamped) and lets go of the pin. Returns how far it moved (+ = down the strip).
    double scrollTo(double top);
    double scrollBy(double dy) { return scrollTo(top_ + dy); }

    // The place under a line of the screen (`screenY` strip units below the screen's top), and under the middle line.
    StripPlace placeAt(double screenY) const;
    StripPlace middle() const { return placeAt(viewHeight_ / 2); }
    // The top that puts `place` at that line of the screen (clamped).
    double topFor(StripPlace place, double screenY) const;

    // Holds `place` at line `screenY` through the changes below (opening a chapter at a spot or at a page's top), until a
    // scroll or unpin(). Moves there now.
    void pin(StripPlace place, double screenY);
    void unpin() { pin_.reset(); }
    bool pinned() const { return pin_.has_value(); }

    // The changes; each keeps the anchor (see above).
    void setHeight(int index, double height);
    void insert(int index, std::vector<StripItem> items);  // before item `index` (size(): at the end)
    void erase(int first, int last);                       // items [first, last)
    void clear();

  private:
    struct Anchor {
        StripPlace place;
        double screenY = 0;
    };
    Anchor anchor() const;
    void restore(const Anchor& anchor);
    void rebuild();

    std::vector<StripItem> items_;
    std::vector<double> tops_{0.0};  // tops_[i] = item i's top; tops_.back() = the content's height
    double top_ = 0;
    double viewHeight_ = 720;
    std::optional<Anchor> pin_;
};

// --- What to load and what to keep on the GPU ---

// The pages to decode around the screen, most urgent first: the items that overlap [top - behind screens, bottom + ahead
// screens] (RuntimeProfile::stripScreensBehind / stripScreensAhead): the page under the middle line first, then the pages
// below it nearest first, then the ones above nearest first. Bands are left out.
std::vector<int> stripPrefetchOrder(const StripLayout& layout, double screensBehind, double screensAhead);
// Whether that window reaches the strip's first item (the previous chapter's page list is wanted) or its last (the next
// chapter's).
struct StripEnds {
    bool top = false;
    bool bottom = false;
};
StripEnds stripWindowEnds(const StripLayout& layout, double screensBehind, double screensAhead);

// A decoded page near the screen: its item, where it is in the strip, and its decoded image's size and tiles (planTiles).
struct StripPageTiles {
    int item = -1;
    double top = 0;
    double height = 0;
    ImageSize decoded;
    std::vector<TileRect> tiles;
};
// One tile to keep as a texture: page item, tile index (in its image's tiles), its place in the strip, and whether it
// overlaps the screen now.
struct StripTile {
    int item = -1;
    int tile = -1;
    double top = 0;
    double bottom = 0;
    bool onScreen = false;
};
// The tiles a strip keeps as textures (RuntimeProfile::webtoonTilesAround = `around`): every row of tiles (the tiles of one
// row share their y) that overlaps the screen [viewTop, viewBottom), plus `around` more rows above and below it, counted
// across page seams. `pages` in strip order. The result is the upload order: the tiles on screen first (top to bottom), then
// the others nearest the screen first (at the same distance the one below first: reading goes down). With no row on screen
// (a band, pages still loading) the rows nearest the screen count from where it is.
std::vector<StripTile> stripTileWindow(const std::vector<StripPageTiles>& pages, double viewTop, double viewBottom,
                                       int around);

// --- Touch, flicks and scrolling ---

// The finger's places (strip units on the reader's screen, + = down) with their times.
struct TouchSample {
    std::int64_t ms = 0;
    double y = 0;
};
// The finger's speed as it lifts at `nowMs` (units per second, + = down): how far it went in its last `windowMs` (the place
// at the window's start interpolated between samples). We measure it ourselves: borealis's pan end assumes every frame takes
// 1/60 s. A finger that rested before lifting (it moved less than kFlingRestUnits in its last kFlingRestMs) gives 0.
inline constexpr int kFlingWindowMs = 100;
inline constexpr int kFlingRestMs = 50;
inline constexpr double kFlingRestUnits = 3;
double releaseVelocity(const std::vector<TouchSample>& samples, std::int64_t nowMs, int windowMs = kFlingWindowMs);

// A flick carries on after the finger lifted and slows down: the speed halves every kFlingHalfLifeSeconds, it stops below
// kFlingStopSpeed, and it starts at most at kFlingMaxScreensPerSecond screens a second.
inline constexpr double kFlingHalfLifeSeconds = 0.25;
inline constexpr double kFlingStopSpeed = 20;  // units per second
inline constexpr double kFlingMaxScreensPerSecond = 5;
// The speed a flick starts with (kept within the top speed for this screen height); 0 below the stop speed (no flick).
double flingStartSpeed(double velocity, double screenHeight);
// How far a flick that started at `velocity` has gone after `seconds` (same sign as the velocity).
double flingOffset(double velocity, double seconds);
// Whether it has stopped by then (and how long it runs in all).
bool flingDone(double velocity, double seconds);
double flingSeconds(double velocity);

// The controller in the strip: a D-pad (or L/R) press glides kStripGlideShare of a screen in kStripGlideMs (eased out,
// glideProgress); held past kStripHoldMs the strip scrolls on at kStripHoldScreensPerSecond until it is let go. A stick
// push scrolls smoothly, faster the further pushed (stickSpeed: WP-13's dead zone and curve), a full push crossing one
// screen in kStripStickCrossSeconds.
inline constexpr double kStripGlideShare = 0.75;
inline constexpr int kStripGlideMs = 250;
inline constexpr int kStripHoldMs = 300;
inline constexpr double kStripHoldScreensPerSecond = 1;
inline constexpr float kStripStickCrossSeconds = 0.6f;
// How far a held D-pad scrolls in `seconds` (units).
double stripHoldScroll(double screenHeight, double seconds);
// How far a stick push `pushY` (-1 .. 1 on the reader's screen, + = down: see more below) scrolls in `seconds` (+ = down).
double stripStickScroll(float pushY, double screenHeight, double seconds, float deadZone = 0.15f);

// Where a tap lands in the strip (on the reader's screen): the top 30 % goes back, the bottom 30 % on (each a glide of
// kStripGlideShare), the middle 40 % opens the menu (a double tap there zooms).
enum class StripTapArea { Back, Middle, On };
StripTapArea stripTapArea(float y, float height);

// --- Zoom in the strip (WP-13d) ---
// Zoom 1 = the strip at its width (stripWidth). Zoomed by z (up to RuntimeProfile::maxZoom), everything in the strip is drawn
// z times as big: it is z x its width wide, and the screen shows its height / z strip units (StripLayout::setViewHeight).
// Sideways, `left` is where the strip's left edge is on the reader's screen (units): centred while the strip fits the
// screen's width, else it pans, with no gap at either side. Up and down, `top` as in StripLayout (strip units at the
// screen's top). A point on the reader's screen (x, y) shows strip point ((x - left) / zoom, top + y / zoom).
struct StripZoomView {
    float zoom = 1;
    double left = 0;
    double top = 0;
};
// The strip's left edge kept on screen: centred when the strip (`drawnWidth` units wide) fits the screen's width, else
// between screenWidth - drawnWidth and 0 (no gap at either side).
double clampStripLeft(double left, double drawnWidth, double screenWidth);
// Zooms from `from` to `to` (kept within 1..maxZoom) around `anchor` (a point on the reader's screen): the strip point under
// it stays under it. `left` comes back kept on screen (clampStripLeft); `top` as wanted: the layout keeps it within the strip
// (StripLayout::scrollTo, once its view height is the screen's height / the new zoom).
StripZoomView stripZoomAround(StripZoomView from, float to, PointF anchor, double stripWidth, double screenWidth,
                              float maxZoom);
// A two-finger pinch in the strip: at its start the view and the two fingers (on the reader's screen). The zoom follows the
// fingers' distance against the distance at the start (kept within 1..maxZoom; fingers that start at one point keep the
// start's zoom), and the strip point that was between them at the start stays between them as they move (it pans with them,
// sideways and up or down). `left` and `top` as in stripZoomAround.
struct StripPinchStart {
    StripZoomView view;
    PointF a;
    PointF b;
};
StripZoomView stripPinchView(const StripPinchStart& start, PointF a, PointF b, double stripWidth, double screenWidth,
                             float maxZoom);
// A double tap in the strip (D48's rule with the strip's zones): the second touch went down within maxGapMs (250) of the
// first tap's lift and within maxDistance (40) units of it, and both lie in the middle band (stripTapArea).
bool isStripDoubleTap(PointF first, std::int64_t firstUpMs, PointF second, std::int64_t secondDownMs, float height,
                      float maxDistance = 40, int maxGapMs = 250);
// The pages that decode again for the zoom (sharper): the page items on screen (overlapping [top, bottom)), nearest the
// middle line first (a page under it first; at the same distance the one below), at most `count`
// (RuntimeProfile::stripZoomPages). Bands are left out.
std::vector<int> stripSharpPages(const StripLayout& layout, int count);

// --- The spot on the server ---

// The chapter meta key the strip saves its exact spot under (other apps ignore it).
inline constexpr std::string_view kStripSpotKey = "snx_strip_spot";
struct StripSpot {
    int page = 0;
    double share = 0;  // 0 .. 1: how far down the page
};
// "<page>:<share>" with 4 decimals, e.g. "2:0.4375" (the share kept within 0..1, the page at least 0). Locale-free.
std::string stripSpotValue(int page, double share);
// The note back. Tolerant: anything that isn't "<page>:<number>" gives nothing; the share is kept within 0..1; more than
// kStripSpotMaxChars characters gives nothing (S6: the note comes from the server).
inline constexpr std::size_t kStripSpotMaxChars = 32;
std::optional<StripSpot> parseStripSpot(std::string_view text);

// --- "By itself": a manga that looks like a webtoon opens as a strip ---

// A tag that is "Long Strip", "Webtoon" or "Webtoons" (any case, the whole tag, spaces around ignored). "Manhwa" and
// "Manhua" don't count (older ones are paged).
bool looksLikeStrip(const std::vector<std::string>& genres);
// A page more than kStripPageShape times as tall as wide (its source size).
inline constexpr double kStripPageShape = 2.5;
bool isStripPage(ImageSize source);

}  // namespace snx::core
