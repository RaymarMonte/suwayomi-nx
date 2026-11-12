#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "ui/reader/ReaderModel.hpp"
#include "ui/reader/ReaderView.hpp"
#include "ui/reader/StripCanvas.hpp"
#include "ui/reader/StripModel.hpp"

// The paged reader (WP-11). Full screen: the page (ReaderView), a message where there is no page (loading, an error with
// Retry, the "previous chapter" / "next chapter" screens), the page number at the bottom, and a menu (top and bottom bars:
// title, Close, Settings) that A or a tap in the middle shows and hides.
// Controller: L/R and the D-pad's left/right turn pages (mirrored for right to left), up/down move a tall page, A the menu,
// X retries, B closes (progress is saved), MINUS opens the reader settings, Y rotates (WP-22: landscape <-> portrait; the
// D-pad and sticks turn with the screen, L/R keep their sides; docked is always landscape). Touch (D16): tap the left/right edge to turn
// (mirrored for right to left), the middle for the menu, swipe sideways to turn, drag to move a page bigger than the screen.
// Zoom (WP-13): pinch, double-tap the middle, ZR / ZL (steps 1, 1.5, 2, 3, 4); zoomed, a drag, either stick and the D-pad
// move around the page, and at its edge a fresh press (or swipe) turns. Holding a button or a stick never turns more than
// one page. Once the zoom has settled (150 ms), the page on screen decodes again at the zoomed size (ReaderModel::setPageZoom).
// Spreads (WP-13c): held sideways (setting "Split spreads": in portrait / always / never) a spread shows as two halves, one
// step each, the right half first for right to left.
// The webtoon strip (WP-13b): reading direction "Vertical strip" (chosen, or by itself for a manga without its own direction
// that is tagged Long Strip / Webtoon or whose first page is a tall strip) shows StripModel + StripCanvas in the same view
// (ReaderView::setStrip); the paged model is parked meanwhile. D-pad up/down and L/R glide, held they scroll on, the sticks
// scroll, taps at the top / bottom glide, the middle opens the menu. The strip keeps its own orientation and width.
// The screen stays on while the reader is open. The state lives in ReaderModel; direction and fit are saved per manga.
namespace snx::ui {

class ReaderActivity : public brls::Activity {
  public:
    // chapters: the manga's chapters known so far (for the next / previous chapter). page: the page index to open at.
    // onClosed: runs once the reader has closed and its progress went to the server (saved or not); the details screen
    // reloads its chapter list then. May be empty; it may run after this screen is gone.
    ReaderActivity(app::AppContext& context, core::Manga manga, std::vector<core::Chapter> chapters, int chapterId,
                   int page, std::function<void()> onClosed);
    ~ReaderActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;
    void onResume() override;  // back from the settings menu

  private:
    void build();
    void modelChanged();
    void updateMessage();
    void updateBars();
    void applyLook();
    void turn(core::Side side);
    void step(int delta);
    void startTurn();
    void toggleMenu();
    void retry();
    void close();
    void openSettings();
    void chooseDirection();
    void chooseFit();
    void chooseBackground();
    void chooseOrientation();
    void setOrientation(core::ReaderOrientation orientation);
    void rotate();
    void followScreen();
    void layoutForScreen();
    // WP-13: the controller once a frame (the D-pad's fresh presses and repeats, the sticks), the zoom settling into a
    // sharper decode, and the frame times of a gesture.
    struct ArrowHold {
        bool down = false;
        bool latched = false;  // it turned a page: nothing until it is let go (one press, one page)
        std::int64_t nextRepeatMs = 0;
    };
    void pollControls();
    void arrowPress(core::Arrow pressed, bool fresh, ArrowHold& hold);
    void pollStick(float seconds);
    void zoomStep(int direction);
    void settleZoom();
    void chooseZoomOnTurn();
    void chooseSpreadSplit();  // WP-13c
    bool readerHasFocus() const;
    void gestureBegin(const std::string& kind);
    void gestureEnd(const std::string& kind);
    void gestureFrame(std::int64_t now);
    void report();
    void reportScreen();
    // WP-13b: the strip.
    void startStrip(int chapterId, int page, StripModel::Open how, const char* rule);
    void stopStrip();
    void checkShape();
    void stripChanged();
    void stripTap(core::StripTapArea area);
    void pollStrip(float seconds);
    std::vector<std::string> stripText(int item);
    void chooseStripWidth();
    void reportStrip();

    app::AppContext& context_;
    std::vector<core::Chapter> chapters_;  // as given (the strip model gets its own copy)
    ReaderModel model_;
    int startChapter_;
    int startPage_;
    std::function<void()> onClosed_;

    ReaderView* view_ = nullptr;
    brls::Box* message_ = nullptr;
    brls::Label* messageTitle_ = nullptr;
    brls::Label* messageText_ = nullptr;
    brls::Button* retryButton_ = nullptr;
    brls::Box* pageNumberBox_ = nullptr;
    brls::Label* pageNumber_ = nullptr;
    brls::Box* topBar_ = nullptr;
    brls::Label* barTitle_ = nullptr;
    brls::Box* bottomBar_ = nullptr;
    brls::Label* barPage_ = nullptr;
    brls::Button* rotateButton_ = nullptr;
    std::vector<brls::Button*> barButtons_;

    bool menuShown_ = false;
    bool closing_ = false;
    core::ReaderBackground background_;
    bool showPageNumber_;
    // WP-22: the saved choice and the portrait way Y goes to; handheld_ as of the last frame (docked = landscape).
    core::ReaderOrientation orientation_;
    core::ReaderOrientation lastPortrait_;
    bool handheld_ = true;
    core::ZoomOnTurn zoomOnTurn_;    // WP-13
    core::SpreadSplit spreadSplit_;  // WP-13c
    // The controller (WP-13): the D-pad by its physical arrow (core::Arrow order), the stick's push under way.
    ArrowHold dpad_[4];
    std::int64_t lastFrameMs_ = -1;
    bool stickPushing_ = false;     // out of the dead zone
    bool stickLatched_ = false;     // it turned a page: nothing until it is back at rest
    bool stickPanning_ = false;     // it moved the page in this push
    bool stickEdgeLeft_ = false;    // the page was at its left / right edge when the push began
    bool stickEdgeRight_ = false;
    // The zoom as the model last got it (setPageZoom), and when the view's last zoom change was seen.
    unsigned seenZoomRevision_ = 0;
    unsigned appliedZoomRevision_ = 0;
    std::int64_t zoomSeenMs_ = 0;
    unsigned seenImageRevision_ = 0;
    std::int64_t sharpMs_ = -1;     // from the last zoom change to the frame that drew the sharper decode, to report once
    // The gesture whose frame intervals are being measured (test events).
    std::string gestureKind_;
    std::int64_t gestureLastMs_ = -1;
    int gestureEndAfter_ = 0;       // a step or a double tap: ends after this many more frames (the one that draws it)
    std::vector<int> gestureFrames_;
    brls::Event<bool>::Subscription focusSubscription_;
    bool subscribed_ = false;

    // The page turn being measured (D17, WP-11 acceptance: < 150 ms when prefetched): when it started, and whether the page
    // was decoded already then. -1: no turn under way.
    std::int64_t turnStartMs_ = -1;
    bool turnReady_ = false;
    int retries_ = 0;       // Retry presses, so a retry that fails again is a new "reader" event
    std::string reported_;  // what the last "reader" event described

    // WP-13b: the strip while it is shown (the canvas draws the model: destroyed first), its settings, and the paged
    // reader's orientation while the strip has its own in orientation_.
    std::unique_ptr<StripModel> strip_;
    std::unique_ptr<StripCanvas> canvas_;
    core::StripWidth stripWidth_;
    core::ReaderOrientation pagedOrientation_;
    std::string stripAuto_;       // "genre" or "shape": the strip was chosen by itself
    bool shapeChecked_ = false;   // the shape rule decides once
    std::int64_t openStartMs_ = -1;
    bool openReported_ = false;
    int waitFramesTotal_ = 0;
    bool holdScrolling_ = false;  // a held D-pad or L/R scrolls on
    std::int64_t holdStartMs_ = -1;
    int holdDirection_ = 0;
    bool stickScrolling_ = false;
};

}  // namespace snx::ui
