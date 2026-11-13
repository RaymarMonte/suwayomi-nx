#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/AppConfig.hpp"
#include "core/ProgressSync.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/StripMath.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "core/models/PageList.hpp"
#include "image/ImageLoader.hpp"
#include "net/CancelToken.hpp"
#include "ui/reader/ProgressSaver.hpp"
#include "ui/reader/ReaderModel.hpp"  // ReaderPage

// The webtoon reader's state (WP-13b): one long vertical strip of pages (core::StripLayout) that holds the chapter being read
// and, once the screen nears an end, its neighbours (at most three chapters; joining a fourth cuts the farthest out), with a
// band between two chapters and at both ends. Where you are is the page under the screen's middle line: its chapter is the
// chapter being read, and its progress (the page and the spot, how far down the page the line is) goes to the server through
// core::ProgressSync (at most every progressSyncMs; everything of a chapter at once when the middle line leaves it; read once
// its last page's bottom is on screen). The pages near the screen (RuntimeProfile::stripScreensBehind / Ahead) decode at the
// strip's width; the others are let go. A page arriving above the screen never moves what you see (the layout's anchor).
// Zoom (WP-13d): zoomed by z the screen shows its height / z strip units; once the zoom and the strip have settled, the pages
// on screen decode again sharper for it (sharpenOnScreen), and a page that leaves the screen lets that decode go.
// Without borealis, so it is unit-tested (FakeHttpClient and a real ImageLoader). StripCanvas draws it; ReaderActivity hosts it.
namespace snx::ui {

// What the strip needs from the profile and the config.
struct StripOptions {
    core::ImageSize units{1280, 720};   // the reader's screen in view units (portrait: 720 x 1280): the strip's layout
    core::ImageSize screen{1280, 720};  // the same in real pixels (docked: 1920 x 1080): pages decode for it
    core::ReaderOrientation orientation = core::ReaderOrientation::Landscape;  // in effect (docked is landscape)
    core::StripWidth width = core::StripWidth::Half;                           // ReaderSettings::stripWidth
    double screensBehind = 1;  // RuntimeProfile::stripScreensBehind
    double screensAhead = 3;   // RuntimeProfile::stripScreensAhead
    float maxZoom = 4;         // RuntimeProfile::maxZoom (WP-13d)
    float maxDecodeScale = 4;  // RuntimeProfile::maxDecodeScale: the sharper decode's cap (core::stripZoomDecodeBox)
    int zoomPages = 2;         // RuntimeProfile::stripZoomPages: at most this many pages on screen decode sharper
    int progressSyncMs = 5000;
    int progressRetryMaxMs = 60000;
    api::PageFormat format = api::PageFormat::AsStored;
    // Milliseconds on a steady clock (the app: appClockMs). Required.
    std::function<std::int64_t()> clock;
    // After every progress save, ok or not (also after the model is gone). May be empty. The "progress" test event.
    std::function<void(const core::ReadProgress& progress, bool ok)> onSaved;
    // WP-15b, the waiting-save queue (it must outlive the model and its saves; nullptr: none). A save that fails as the
    // strip closes goes there; the chapter the strip opens takes its entry over (its page and spot; not a chapter that
    // joins later while scrolling: the queue's round decides that one).
    WaitingSaves* queue = nullptr;
};
// Everything above except units, screen, orientation, clock, onSaved and queue.
StripOptions stripOptionsFrom(const core::RuntimeProfile& profile, const core::ReaderSettings& reader);

// A band of the strip: the chapters above and below it (0 = none: the strip's very start or end), and, for the strip's first
// or last band, the neighbouring chapter's page list it waits for.
struct StripBand {
    int above = 0;  // chapter ids
    int below = 0;
    enum class Edge { None, Loading, Failed } edge = Edge::None;
    api::ApiError error;  // Failed: why (a tap or X retries)
};

class StripModel {
  public:
    enum class State {
        Loading,  // the opened chapter's page list is on its way
        Ready,
        Failed,   // it didn't come: error() says why; retry()
    };
    // How open() places the chapter. Resume: the spot note goes under the middle line when it is for `page` and the chapter
    // isn't read, else `page`'s top at the screen's top. PageTop: always `page`'s top at the top (switching from the page
    // reader keeps the page being read).
    enum class Open { Resume, PageTop };

    // `chapters`: the manga's chapters as far as they are known (any order; the strip joins them by sourceOrder).
    // `client` and `images` must outlive the model.
    StripModel(api::SuwayomiClient& client, image::ImageLoader& images, core::Manga manga,
               std::vector<core::Chapter> chapters, StripOptions options);
    // Cancels every load. Progress that isn't saved yet still goes out (like close() without a callback).
    ~StripModel();

    StripModel(const StripModel&) = delete;
    StripModel& operator=(const StripModel&) = delete;

    // Called after every change other than a plain scroll (loads, the page or chapter under the middle line, the layout).
    void setListener(std::function<void()> listener);

    // Opens chapter `chapterId` at page `page` (kept inside the chapter). Only once; the neighbours join by scrolling.
    void open(int chapterId, int page, Open how = Open::Resume);
    void retry();  // after Failed: the page list again
    // A failed page, or the strip's end band whose chapter list failed, at layout item `item`: loads it again. False if
    // there is nothing to retry there.
    bool retryItem(int item);
    // X: the failed page on screen nearest the middle line, else a failed end band on screen. False if none.
    bool retryNearest();

    // Scrolling (strip units, + = down the strip). Returns how far it moved (kept within the strip).
    double scrollBy(double dy);
    double scrollTo(double top);

    // The reader's screen (view units and real pixels) and the orientation in effect (WP-22): the strip's width, the bands
    // and the decode box follow; the place under the middle line stays.
    void setScreen(core::ImageSize units, core::ImageSize screen, core::ReaderOrientation orientation);
    void setStripWidth(core::StripWidth width);
    core::StripWidth stripWidthSetting() const { return options_.width; }
    core::ImageSize units() const { return options_.units; }  // the reader's screen in view units

    // Once per frame: sends the progress when it's due.
    void onFrame();
    void saveNow();  // everything not saved yet, now (HOME, sleep)
    // The reader closes: everything not saved yet goes, then `done` runs once every save has ended (at once if none is on
    // its way). `done` may run after the model is gone.
    void close(std::function<void()> done);

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const core::Manga& manga() const { return manga_; }

    // The strip.
    const core::StripLayout& layout() const { return layout_; }
    double stripWidth() const;                  // in view units
    core::ImageSize decodeBox() const;          // what strip pages decode at (real pixels)
    const ReaderPage& pageAt(int item) const;   // a page item's page (a Waiting one for a band or a wrong index)
    StripBand band(int item) const;             // a band item's chapters ({} for a page)
    const core::Chapter* chapterById(int id) const;  // any of the manga's chapters (as last known), nullptr if unknown
    std::vector<int> chaptersHeld() const;      // the chapters in the strip, top to bottom
    int pagesHeld() const;                      // pages decoded and held (Ready)
    // The image to draw for a page item: its sharper decode once it is there (WP-13d), else its own; nullptr while it loads.
    image::ImagePtr shownImage(int item) const;

    // Where you are: the chapter being read (the one under the middle line last), its page under the line and the spot.
    const core::Chapter& chapter() const;
    int page() const { return page_; }      // -1 before the strip is ready
    double spot() const { return spot_; }   // 0 .. 1
    int pageCount() const;                  // the chapter being read's pages
    bool chapterRead() const;               // read on the server, or since its last page's bottom showed

    // --- Zoom (WP-13d) ---
    // The zoom on screen (1 = the strip at its width): the screen shows units.height / zoom strip units. Not saved: a new
    // screen (rotating, docking) or strip width goes back to 1.
    float zoom() const { return zoom_; }
    float maxZoom() const { return options_.maxZoom; }
    // The zoom and the strip y at the screen's top at once (a zoom around a point, a pinch): the zoom kept within 1..maxZoom,
    // the top within the strip.
    void setView(float zoom, double top);
    // The zoom and the strip have settled (the activity waits 150 ms, D48): the pages on screen (core::stripSharpPages, at
    // most zoomPages) decode again at the zoom's box (core::stripZoomDecodeBox) at Reader priority, unless the decode they
    // have already has the detail it wants (a webtoon strip 720 .. 1080 wide has none to give above about 1.1 .. 1.5 x: the
    // "no sharper" rule, WP-13c). A sharper decode stays drawn until the next one is there. At zoom 1 every sharper decode
    // goes, and so does that of a page no longer among the pages on screen. Calling it again changes nothing (cheap).
    void sharpenOnScreen();
    // The box a page wants at the zoom on screen (its own box at zoom 1): what "sharp" is measured against (test events).
    core::ImageSize zoomBox() const;
    bool sharpLoading() const;  // a sharper decode is on its way (the idle check)
    int sharpHeld() const;      // pages with a sharper decode now

  private:
    struct Slot {
        ReaderPage page;
        std::string url;  // full URL; empty: the server listed the page in a form we can't use
        net::CancelToken cancel;
        std::optional<core::ImageSize> source;  // known once decoded; kept when let go, so the height stays
        // WP-13d: the page decoded again for the zoom (only pages on screen), the box asked for, and that load.
        image::ImagePtr sharp;
        core::ImageSize sharpBox;
        bool sharpLoading = false;
        net::CancelToken sharpCancel;
    };
    // One chapter in the strip.
    struct Part {
        core::Chapter chapter;
        std::vector<Slot> slots;
        std::optional<core::ImageSize> shape;  // the last decoded page's source size: the estimate for pages not known yet
    };
    // A neighbouring chapter's page list, asked for when the window nears that end of the strip.
    struct Edge {
        int chapterId = 0;  // 0: none asked for
        bool loading = false;
        bool failed = false;
        api::ApiError error;
        net::CancelToken cancel;
    };

    void load(int chapterId, int page, Open how);
    void onOpened(int page, Open how, const api::ApiResult<core::PageList>& result);
    Part makePart(int chapterId, const core::PageList& list) const;
    std::vector<core::StripItem> itemsFor(const Part& part) const;
    double heightFor(const Part& part, const Slot& slot) const;
    void requestEdge(bool below);
    void onEdge(bool below, int chapterId, const api::ApiResult<core::PageList>& result);
    void join(bool below, Part part);
    void cutFarthest();
    void arrived(Part& part, int index, image::ImagePtr image);
    void refreshHeight(Part& part, int index);
    core::StripItem bandItem() const;
    int firstItem(std::size_t part) const;  // part k's first page item (k + 1 + the pages above)
    int bandIndexOf(int item) const;        // band k (above part k), -1 if the item isn't a band
    std::optional<double> noteSpot(const core::Chapter& chapter) const;
    void update();          // the reading chapter, the progress, the window and the ends, after any change
    bool followMiddle();    // the reading chapter and page from the middle line; true if they changed
    bool partRead(const Part& part) const;
    void startReading(Part& part);
    void updateWindow();
    void startLoad(Part& part, int index, net::Priority priority);
    void onLoaded(int chapterId, int index, const image::ImageResult& result);
    void reloadNetworkFailures();
    void resetSlot(Slot& slot);
    bool dropSharp(Slot& slot);  // true if it had one (or one on its way)
    void dropSharpOffScreen();
    void requestSharp(Part& part, int index, core::ImageSize box);
    void relayout();        // every height again (the width, the screen, the estimates)
    void decodeAgain();     // every page again, at the new decode box
    void onSaved(const core::ReadProgress& progress);
    Part* partOf(int chapterId);
    const Part* partOf(int chapterId) const;
    const Slot* slotAt(int item) const;
    int chapterIndex(int chapterId) const;  // in chapters_, -1 if unknown
    void changed();

    api::SuwayomiClient& client_;
    image::ImageLoader& images_;
    core::Manga manga_;
    std::vector<core::Chapter> chapters_;  // by sourceOrder, oldest first
    StripOptions options_;
    std::function<void()> listener_;
    std::shared_ptr<ProgressSaver> saver_;

    bool opened_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    int openChapter_ = 0;  // for retry
    int openPage_ = 0;
    Open openHow_ = Open::Resume;
    net::CancelToken listCancel_;

    std::vector<Part> parts_;  // the chapters in the strip, top to bottom (by sourceOrder)
    core::StripLayout layout_;
    Edge above_;
    Edge below_;
    int readingId_ = 0;  // the chapter being read
    int page_ = -1;
    double spot_ = 0;
    float zoom_ = 1;  // WP-13d
    std::shared_ptr<core::ProgressSync> sync_;  // the chapter being read's progress
    bool updating_ = false;                     // update() runs (a join from a synchronous answer waits for it)
    bool again_ = false;                        // ... and something asked for it meanwhile
    bool moved_ = false;                        // the reading chapter or page changed since the last changed()
};

}  // namespace snx::ui
