#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/ReaderMath.hpp"
#include "core/StripMath.hpp"
#include "image/Caches.hpp"
#include "ui/reader/StripModel.hpp"

// Draws the webtoon strip (WP-13b) inside ReaderView, which keeps the screen, the portrait transform, the overlay, the
// buttons and the touch handling for both readers (ReaderView::setStrip). The strip is centred on the reader's screen at
// StripModel::stripWidth(); its pages are drawn from GPU tiles (one texture per tile, each reserved in the TextureBudget, S7).
// The tiles kept are core::stripTileWindow's (the rows on screen plus RuntimeProfile::webtoonTilesAround rows above and
// below); the ones on screen upload at once, the others at most one per frame (nearest first), and the ones that leave are
// freed (if the budget refuses one, the farthest go first). A page still loading is a placeholder with the spinner; a failed
// page and the bands between chapters show their text (the activity gives it, in the app's language).
// It also runs the strip's motion from the clock: the flick after a drag, the glides of the D-pad and taps; the held D-pad
// and the sticks scroll it by scroll() and panBy().
// Zoom (WP-13d): the strip is drawn StripModel::zoom() times as big (core's StripZoomView rules): zoomed wider than the screen
// it pans sideways too (a drag, the D-pad and sticks left/right). The zoom changes around a point (a double tap, ZR/ZL around
// the middle, a pinch between the fingers). When the model has a sharper decode of a page, its textures are made first and
// the page's old ones freed only after: if they don't all fit, the old ones stay (S7). Main thread only.
namespace snx::ui {

class StripCanvas {
  public:
    StripCanvas(StripModel& model, image::TextureBudget& budget, int tilesAround);
    ~StripCanvas();  // frees the textures

    StripCanvas(const StripCanvas&) = delete;
    StripCanvas& operator=(const StripCanvas&) = delete;

    // The lines of text for a band or a failed page at layout item `item` (the activity's i18n texts).
    std::function<std::vector<std::string>(int item)> textFor;
    // A gesture began or ended: "fling", "glide" (the activity measures its frames and writes "gesture" events).
    std::function<void(const char* kind, bool started)> onGesture;
    void setTextColor(NVGcolor color) { textColor_ = color; }

    // Once per frame, before draw(): the flick and the glide move on.
    void tick();
    // The strip on the reader's screen (reader units from (x, y)).
    void draw(NVGcontext* vg, float x, float y, core::SizeF screen);
    bool ready() const { return model_.state() == StripModel::State::Ready; }

    // --- Touch (ReaderView) ---
    // A finger touched down: a flick stops, and true is returned (that touch is then no tap). A glide runs on (a tap adds
    // to it, a drag stops it).
    bool touchDown();
    // A drag: the finger's offset from where the drag began (reader units: + y = down, + x = right), at `ms`. The strip
    // follows it: up and down, and sideways when it is wider than the screen (zoomed).
    void dragStart(std::int64_t ms);
    void dragTo(core::PointF offset, std::int64_t ms);
    // The finger lifted: a flick carries on if it was moving (core::releaseVelocity; up and down only).
    void dragEnd(std::int64_t ms);
    // A second finger came down during a drag: the drag ends where it is, with no flick.
    void dragCancel();

    // --- Zoom (WP-13d; ReaderView's pinch and double tap, the activity's ZR / ZL) ---
    float zoom() const { return model_.zoom(); }
    // The strip's left edge on the reader's screen (units): centred while it fits, else kept so no gap shows at a side.
    double left() const;
    // Zoom to `zoom` (kept within 1..maxZoom) around `anchor` on the reader's screen: the strip point under it stays.
    void zoomTo(float zoom, core::PointF anchor);
    // ZR / ZL: one step in (+1) or out (-1) around the middle of the screen (core::zoomStep).
    void zoomStep(int direction);
    // Two fingers: the pinch's start, the fingers' places as they move, and the end (let go below 1.05: back to 1).
    void pinchStart(core::PointF a, core::PointF b);
    void pinchTo(core::PointF a, core::PointF b);
    void pinchEnd();
    void cancelPinch() { pinch_.reset(); }  // the screen turned: the pinch is over
    bool pinching() const { return pinch_.has_value(); }
    // Goes up with every finished zoom change (a step, a double tap, the end of a pinch).
    unsigned zoomRevision() const { return zoomRevision_; }

    // --- Controller and taps (the activity) ---
    // A glide of core::kStripGlideShare of a screen: +1 on (down the strip), -1 back. A press during a glide adds to it.
    void glide(int direction);
    // Zoomed: a glide sideways by kStripGlideShare of the screen's width: +1 shows more on the right, -1 on the left.
    void glideSideways(int direction);
    // Moves the strip at once by `dy` units (held D-pad, sticks): a flick or glide stops. Returns how far it moved.
    double scroll(double dy);
    // Zoomed: moves the view `dx` units to the right at once (+ = see more on the right). Returns how far it moved.
    double panBy(double dx);
    bool moving() const { return fling_.has_value() || glide_.has_value() || sideGlide_.has_value(); }

    // --- For the test events ---
    int tilesHeld() const;
    // The decoded size the page at layout item `item` is drawn from now ({0, 0}: none yet): a sharper decode once its
    // textures are in.
    core::ImageSize drawnSize(int item) const;
    // Goes up whenever a page's textures change over to another decode of it (a sharper one, or back), and how long that
    // frame's uploads took (ms).
    unsigned swapRevision() const { return swaps_; }
    float swapUploadMs() const { return swapUploadMs_; }
    // Frames since the last call in which part of the screen showed a page (or a tile) still loading.
    int takeWaitFrames();
    bool waiting() const { return waitingNow_; }  // the last frame showed a page (or tile) still loading
    float uploadMs() const { return uploadMs_; }  // the GPU uploads in the last frame that made any
    bool refused() const { return refused_; }     // the budget had no room for a tile on screen in the last frame
    // Goes up with every finished move (drag, flick, glide, scroll stop), like ReaderView::moveRevision.
    unsigned moveRevision() const { return moves_; }
    // A held button or a stick stopped scrolling: the move is done (and the strip comes to rest on whole units).
    void finishScroll();

  private:
    struct PageKey {
        int chapterId = 0;
        int page = 0;
        bool operator<(const PageKey& other) const {
            return chapterId != other.chapterId ? chapterId < other.chapterId : page < other.page;
        }
    };
    struct Key {
        PageKey page;
        int tile = 0;
        bool operator<(const Key& other) const {
            if (page < other.page || other.page < page) {
                return page < other.page;
            }
            return tile < other.tile;
        }
        bool operator==(const Key& other) const { return !(*this < other) && !(other < *this); }
    };
    struct Held {
        int texture = 0;
        image::TextureLease lease;
    };
    // One page's textures: the decode they come from (its size and tiles) and the tiles made so far.
    struct HeldPage {
        core::ImageSize decoded;
        std::vector<core::TileRect> rects;
        std::map<int, Held> tiles;  // by tile index
        core::ImageSize refused;    // a sharper decode of this size found no room: not tried again for this page
    };
    struct Fling {
        double velocity = 0;   // screen units per second at the start (the strip moves that / the zoom)
        std::int64_t startMs = 0;
        double done = 0;       // the distance already gone (screen units)
    };
    struct Glide {
        double distance = 0;   // strip units
        std::int64_t startMs = 0;
        double done = 0;
    };
    struct SideGlide {
        double from = 0;  // the strip's left edge
        double to = 0;
        std::int64_t startMs = 0;
    };

    void updateTextures(NVGcontext* vg);
    bool swapTo(NVGcontext* vg, HeldPage& held, const image::ImagePtr& image, int item);
    bool makeTexture(NVGcontext* vg, const image::ImagePtr& image, int tile, Held& out);
    bool upload(NVGcontext* vg, const Key& key, const image::ImagePtr& image, const std::vector<Key>& keepOrder);
    void freeTile(NVGcontext* vg, Held& held);
    void freePage(NVGcontext* vg, HeldPage& page);
    void drawItem(NVGcontext* vg, int item, float left, float top, float width, float height, core::SizeF screen,
                  float x, float y, bool& waiting);
    void drawText(NVGcontext* vg, const std::vector<std::string>& lines, float cx, float cy, float maxWidth);
    void drawSpinner(NVGcontext* vg, float cx, float cy);
    void stopMotion();
    void endGlide();
    void restOnPixels();
    core::SizeF screen() const;  // the reader's screen (units)
    double drawnWidth() const;   // the strip's width x the zoom
    static std::int64_t nowMs();

    StripModel& model_;
    image::TextureBudget& budget_;
    int tilesAround_;
    std::map<PageKey, HeldPage> held_;
    NVGcolor textColor_ = nvgRGB(200, 200, 200);
    std::vector<core::TouchSample> samples_;  // the drag's last moments
    core::PointF dragOffset_;
    double dragLeft_ = 0;    // the left edge when the drag began
    double left_ = 0;        // the strip's left edge as last placed (read it through left(): kept on screen)
    std::optional<core::StripPinchStart> pinch_;
    core::PointF pinchAt_;   // the fingers' middle, last seen
    unsigned zoomRevision_ = 0;
    std::optional<Fling> fling_;
    std::optional<Glide> glide_;
    std::optional<SideGlide> sideGlide_;
    int waitFrames_ = 0;
    bool waitingNow_ = false;
    float uploadMs_ = 0;
    bool refused_ = false;
    unsigned moves_ = 0;
    unsigned swaps_ = 0;
    float swapUploadMs_ = 0;
};

}  // namespace snx::ui
