#pragma once

#include <borealis.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/AppConfig.hpp"
#include "core/ReaderMath.hpp"
#include "core/StripMath.hpp"
#include "image/Caches.hpp"
#include "image/DecodedImage.hpp"

// The reader's page area (WP-11): fills the screen, draws one decoded page from its GPU tiles (one texture per tile, SAFETY.md
// S7: reserved in the TextureBudget) at the size of the fit, and turns touch into reader input (D16): a tap reports its zone
// (left / middle / right), a drag moves a page that is bigger than the screen, and a sideways swipe reports the side to turn
// to (only when the page was already at its edge that way). The activity's views (messages, page number, menu bars) go into
// overlay() and draw on top.
// Portrait (WP-22): everything (page, spinner, overlay) is drawn inside one NanoVG transform (core::readerTransform), the
// overlay laid out for the reader's screen (720 x 1280 in handheld). borealis finds touched views by their unturned boxes, so
// in portrait this view takes every touch itself (hitTest), maps it with core::screenToReader, and runs the action of an
// addButton() button under the mapped point; anything else is a tap zone, a drag or a swipe on the reader's screen.
// Zoom (WP-13): the page is drawn zoom() times its fit (1 = the fit, up to maxZoom). Two fingers pinch (our own recognizer
// keeps every finger by its id: borealis's pan follows only one); while two fingers are down no tap, drag or swipe counts,
// and after a pinch nothing counts until every finger has lifted. A double tap in the middle zooms to 2 x at that point (or
// back to the fit); so a single middle tap waits 250 ms before it reports. The page may be swapped for a sharper decode of
// itself (the same page key) without moving. Glides (the D-pad) run from the clock.
// Split spreads (WP-13c): the view may draw one half of the image (the textures still hold all of it): fit, zoom and pan
// treat that half as the page, and each tile is cut to the half's box where that box lies inside the screen.
// Main thread only.
namespace snx::ui {

class StripCanvas;

class ReaderView : public brls::Box {
  public:
    explicit ReaderView(image::TextureBudget& textures);
    ~ReaderView() override;  // frees the textures

    // The page to draw (nullptr: none), its fit, the reading direction (where a page wider than the screen starts), the
    // page's key (which page of which chapter, and which half) and the half of a split spread to draw (WP-13c: the image's
    // left or right half, drawn like a page of its own; nothing = the whole image). A new page, half or fit starts at the
    // page's start position (the top; core::startPosition), back at the fit unless keepZoomOnTurn. Another image of the same
    // page (a sharper decode, WP-13) replaces the textures and keeps the zoom and position; the new textures are reserved and
    // made first, and if they don't fit the old ones stay.
    void showPage(const image::ImagePtr& image, core::PageFit fit, core::ReadingDirection direction,
                  const std::string& pageKey, std::optional<core::Side> half = std::nullopt);
    // The GPU budget had no room for the page (S7): it isn't drawn.
    bool textureRefused() const { return refused_; }
    bool hasPage() const { return !textures_.empty(); }
    // The image drawn now (nullptr: none): its decoded size is what the "sharp" rule looks at.
    const image::ImagePtr& image() const { return image_; }
    // Goes up with every new page (or half) shown (the activity's test events tell one page from the next). Not for a sharper
    // decode.
    unsigned pageRevision() const { return revision_; }
    // Goes up whenever another image of the same page is swapped in.
    unsigned imageRevision() const { return imageRevision_; }
    // How long the last swap took to make its textures (the GPU upload, in the frame that swaps), in ms: test events.
    float uploadMs() const { return uploadMs_; }
    // Moves a page taller than the screen up or down (not zoomed, D-pad), in screen units.
    void scrollBy(float dy);
    // The drawn page's (or half's) size, for the test events. {0, 0} without a page.
    core::SizeF pageSize() const { return drawn_; }
    // Where the page's top-left corner is on the reader's screen, and a count of finished moves (drags, scrolls, glides,
    // stick pans), for the test events: a move changes no page, so the activity reports it by this count.
    core::PointF pagePosition() const { return position_; }
    unsigned moveRevision() const { return moves_; }

    // --- Zoom (WP-13) ---
    float zoom() const { return zoom_; }
    void setMaxZoom(float maxZoom) { maxZoom_ = maxZoom; }
    // Keep the zoom when a new page comes ("Zoom on page turn: Keep the zoom"); else a new page shows whole.
    void setKeepZoomOnTurn(bool keep) { keepZoomOnTurn_ = keep; }
    // Zoom to `zoom` (kept within 1..maxZoom) around `anchor` on the reader's screen; nothing without a page.
    void zoomTo(float zoom, core::PointF anchor);
    // ZR / ZL: one step in (+1) or out (-1) around the middle of the screen.
    void zoomStep(int direction);
    void resetZoom();  // back to the fit, centred
    // Goes up with every finished zoom change (a step, a double tap, the end of a pinch, a reset), and when it was.
    unsigned zoomRevision() const { return zoomRevision_; }
    std::int64_t zoomChangedMs() const { return zoomChangedMs_; }
    // Two fingers are on the page (the zoom follows them; the decode waits until they lift).
    bool pinching() const { return pinch_.has_value(); }
    // Whether more of the page lies beyond the screen's edge toward `arrow`, from where it is going (a glide's end).
    bool canMove(core::Arrow arrow) const;
    // Moves the page's corner by `move` (clamped), at once: a stick's pan. True if it moved.
    bool panBy(core::PointF move);
    // A stick pan stopped (counts as one finished move for the test events).
    void finishPan() { ++moves_; }
    // Moves the page's corner by `move` (clamped), gliding there for core::kArrowGlideMs: a D-pad press. A press during a
    // glide adds to where it is going.
    void glideBy(core::PointF move);
    bool gliding() const { return glide_.has_value(); }

    // Taps: a single middle tap waits for a possible second one (a double tap zooms) while this is on; with the menu open
    // the activity turns it off (any tap then closes the menu at once).
    void setDoubleTapWait(bool on);
    bool tapWaiting() const { return pendingTap_.has_value(); }

    void setSpinner(bool on) { spinner_ = on; }  // the page is loading: a turning arc in the middle

    // Strip mode (WP-13b): the view draws `strip` instead of a page (nullptr: back to pages; the page shown before is gone).
    // Touch then drives the strip: a drag scrolls it, a quick lift flicks it on, a touch stops a flick (and is no tap), a tap
    // reports its strip area (onStripTap). No swipes, pinches or double taps in strip mode (zoom in the strip is WP-13d).
    void setStrip(StripCanvas* strip);
    bool stripMode() const { return strip_ != nullptr; }

    // The box the activity's views go into: the reader's screen, laid out upright for the orientation.
    brls::Box* overlay() const { return overlay_; }
    // How the console is held; `screen` = this view's real (landscape) size in units. The page starts again at its start,
    // at the fit.
    void setOrientation(core::ReaderOrientation orientation, core::SizeF screen);
    core::ReaderOrientation orientation() const { return orientation_; }
    // The reader's screen in units (720 x 1280 in portrait handheld).
    core::SizeF readerScreen() const { return core::readerScreen(orientation_, real_); }
    // A button in the overlay: in portrait a mapped tap on it runs `action` (borealis can't find it there). In landscape
    // borealis runs the button's own click action as usual.
    void addButton(brls::View* button, std::function<void()> action);

    std::function<void(core::TapArea)> onTap;
    // The webtoon strip (WP-13b): a tap in strip mode reports its area (top, middle, bottom of the reader's screen) at once.
    std::function<void(core::StripTapArea)> onStripTap;
    std::function<void(core::Side)> onSwipe;
    std::function<void()> onFrame;  // before every frame it draws (the activity reads the controller there)
    std::function<void()> onDrawn;  // after every frame it drew (the activity writes its test events there)
    // A gesture began or ended: "pinch", "drag", "glide", "double_tap" (the activity measures its frames).
    std::function<void(const char* kind, bool started)> onGesture;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;
    brls::View* hitTest(brls::Point point) override;
    // borealis skips drawing a child outside its parents' boxes: in portrait the box is the reader's screen (720 x 1280).
    void getCullingBounds(float* top, float* right, float* bottom, float* left) override;

  private:
    struct Button {
        brls::View* view;
        std::function<void()> action;
    };
    struct Finger {
        core::PointF at;    // on the reader's screen
        unsigned pass = 0;  // the input pass that last saw it
    };
    struct Pinch {
        core::PinchStart start;
        int a = 0;  // the two fingers' ids
        int b = 0;
    };
    struct Glide {
        core::PointF from;
        core::PointF to;
        std::chrono::steady_clock::time_point started;
    };
    struct PendingTap {
        core::PointF at;
        std::int64_t upMs = 0;
        bool secondDown = false;  // a second touch went down in time, close by: its end decides
    };
    class PinchRecognizer;

    void freeTextures();
    bool makeTextures(const image::ImagePtr& image, std::vector<int>& textures, std::vector<image::TextureLease>& leases);
    void layout(core::SizeF screen);
    void setZoomValue(float zoom);
    void zoomChanged();
    void gesture(const char* kind, bool started);
    void onTapped(brls::Point position);
    void onMiddleTap(core::PointF at);
    void firePendingTap();
    void onPan(const brls::PanGestureStatus& status);
    void onFinger(const brls::TouchState& touch);
    void startPinch();
    void endPinch();
    void tick();
    core::PointF toReader(brls::Point position);
    bool shown(brls::View* view);
    void drawPage(NVGcontext* vg, float x, float y, core::SizeF screen);
    void drawSpinner(NVGcontext* vg, float cx, float cy);
    static std::int64_t nowMs();

    image::TextureBudget& budget_;
    image::ImagePtr image_;
    std::string pageKey_;
    std::optional<core::Side> half_;  // WP-13c: the half of the image drawn; nothing = all of it
    std::vector<int> textures_;  // one per tile, in image_->tiles() order
    std::vector<image::TextureLease> leases_;
    bool refused_ = false;
    unsigned revision_ = 0;
    unsigned imageRevision_ = 0;
    float uploadMs_ = 0;
    unsigned moves_ = 0;
    core::PageFit fit_ = core::PageFit::Screen;
    core::ReadingDirection direction_ = core::ReadingDirection::RightToLeft;
    bool placed_ = false;  // position_ is valid for the current page, fit, zoom and screen
    core::SizeF screen_;
    core::SizeF fitSize_;  // the page at its fit (zoom 1)
    core::SizeF drawn_;    // fitSize_ x zoom_
    core::PointF position_;
    float zoom_ = 1;
    float maxZoom_ = 4;
    bool keepZoomOnTurn_ = false;
    unsigned zoomRevision_ = 0;
    std::int64_t zoomChangedMs_ = 0;
    // The touch under way: where and when the finger touched down, and the drag (if borealis calls it one).
    brls::Point touchDown_;
    std::chrono::steady_clock::time_point touchDownAt_;
    core::PointF dragFrom_;
    bool dragging_ = false;
    bool couldMoveLeft_ = false;
    bool couldMoveRight_ = false;
    // Every finger on the page by its id (WP-13), whether this touch has had a second finger, and the pinch under way.
    std::map<int, Finger> fingers_;
    unsigned pass_ = 1;
    bool multiTouch_ = false;
    std::optional<Pinch> pinch_;
    std::optional<Glide> glide_;
    bool doubleTapWait_ = true;
    std::optional<PendingTap> pendingTap_;
    bool spinner_ = false;
    brls::Box* overlay_ = nullptr;  // a child: borealis deletes it
    core::ReaderOrientation orientation_ = core::ReaderOrientation::Landscape;
    core::SizeF real_{1280, 720};
    std::vector<Button> buttons_;
    StripCanvas* strip_ = nullptr;  // the activity's
    bool swallowTap_ = false;       // this touch stopped a flick: it is no tap
};

}  // namespace snx::ui
