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
#include "core/ReaderMath.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "core/models/PageList.hpp"
#include "image/ImageLoader.hpp"
#include "net/CancelToken.hpp"

// The paged reader's state (WP-11): the opened chapter's pages, the position (a page, or the "previous chapter" and "next
// chapter" screens before the first and after the last page), the reading direction and fit, the pages loaded around the
// position (RuntimeProfile's prefetch window; everything outside it is cancelled and let go), the next chapter's first pages
// near the end of a chapter, and the reading progress sent to the server (core::ProgressSync). Without borealis, so it is
// unit-tested (FakeHttpClient and a real ImageLoader). ReaderActivity shows it.
namespace snx::ui {

// What the reader needs from the profile and the config.
struct ReaderOptions {
    core::ImageSize screen{1280, 720};  // the screen in real pixels: pages decode for it (core::decodeBoxFor)
    int prefetchBehind = 1;             // RuntimeProfile::prefetchBehind
    int prefetchAhead = 3;              // RuntimeProfile::prefetchAhead
    int nextChapterPages = 2;           // RuntimeProfile::nextChapterPrefetchPages
    bool backgroundPrefetch = true;     // RuntimeProfile::backgroundPrefetch: the next chapter's pages near the end
    int progressSyncMs = 5000;          // RuntimeProfile::progressSyncMs
    int progressRetryMaxMs = 60000;     // RuntimeProfile::progressRetryMaxMs
    api::PageFormat format = api::PageFormat::AsStored;  // ReaderSettings::serverConvertsToJpeg
    // Milliseconds on a steady clock, for the progress timing (the app: appClockMs). Required.
    std::function<std::int64_t()> clock;
    // Called after every progress save, ok or not, on the client's deliver thread, also after the model is gone (the save
    // when the reader closes). May be empty. The app writes the "progress" test event here.
    std::function<void(const core::ReadProgress& progress, bool ok)> onSaved;
};
// Everything above except screen, clock and onSaved.
ReaderOptions readerOptionsFrom(const core::RuntimeProfile& profile, const core::ReaderSettings& reader);

// One page of the opened chapter.
struct ReaderPage {
    enum class State {
        Waiting,  // not asked for: outside the window, or prefetching is paused (memory is low)
        Loading,
        Ready,
        Failed,  // error says why; retryPage() tries again, and so does coming back to it from outside the window.
                 // A page that failed because the server didn't answer (offline, timeout) also loads again when it is
                 // turned to, and as soon as another page of the chapter arrives (WP-12: the server may be back)
    };
    State state = State::Waiting;
    image::ImagePtr image;  // Ready
    image::ImageError error;
};

class ReaderModel {
  public:
    enum class State {
        Loading,  // the chapter's page list is on its way
        Ready,
        Failed,   // the page list didn't come: error() says why; retry()
    };

    // `chapters`: the manga's chapters as far as they are known (any order; the reader goes through them by sourceOrder).
    // `settings`: the direction and fit to start with (readerSettingsFor). `client` and `images` must outlive the model.
    ReaderModel(api::SuwayomiClient& client, image::ImageLoader& images, core::Manga manga,
                std::vector<core::Chapter> chapters, core::MangaReaderSettings settings, ReaderOptions options);
    // Cancels every load. Progress that isn't saved yet still goes out (like close() without a callback).
    ~ReaderModel();

    ReaderModel(const ReaderModel&) = delete;
    ReaderModel& operator=(const ReaderModel&) = delete;

    // Called after every change (through the deliver threads: the main thread in the app).
    void setListener(std::function<void()> listener);

    // Opens chapter `chapterId` at page index `page` (kept inside the chapter). Only once; later chapters open by step().
    void open(int chapterId, int page);
    void retry();      // after Failed: the page list again
    void retryPage();  // the page on screen failed: loads it again

    // One step: +1 = next, -1 = previous. Past the "next chapter" screen it opens the next chapter (where it was left, or at
    // the start if it is read); before the "previous chapter" screen the previous chapter at its last page. False when there
    // is no chapter that way or the page list isn't there (loading, failed).
    bool step(int delta);
    // A key, tap or swipe toward `side`: step(core::pageStep(side, direction())).
    bool turn(core::Side side);

    void setDirection(core::ReadingDirection direction);
    void setFit(core::PageFit fit);  // the pages load again, decoded for the new fit

    // Once per frame: sends the progress when it's due (core::ProgressSync).
    void onFrame();
    // Sends everything not saved yet now (HOME, sleep).
    void saveNow();
    // The reader is closing: sends everything not saved yet, then calls `done` once every save has ended (ok or not; a failed
    // one isn't tried again). Right away if nothing is on its way. `done` may run after the model is gone.
    void close(std::function<void()> done);

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const core::Manga& manga() const { return manga_; }
    const core::Chapter& chapter() const { return chapter_; }  // the opened chapter, as the server answered last
    const core::Chapter* nextChapter() const;      // by sourceOrder; nullptr if there is none
    const core::Chapter* previousChapter() const;
    int pageCount() const { return static_cast<int>(slots_.size()); }
    // -1 = the "previous chapter" screen, pageCount() = the "next chapter" screen, else the page on screen.
    int position() const { return position_; }
    bool onPage() const { return position_ >= 0 && position_ < pageCount(); }
    const ReaderPage& page(int index) const;  // a page of the opened chapter (a Waiting one for a wrong index)
    const ReaderPage& current() const { return page(position_); }
    core::ReadingDirection direction() const { return settings_.direction; }
    core::PageFit fit() const { return settings_.fit; }
    const core::MangaReaderSettings& settings() const { return settings_; }  // to remember in config.json
    core::ImageSize pageBox() const;  // what pages decode at for the fit
    bool chapterRead() const;  // the opened chapter is read (on the server, or since its last page was shown)
    // The next chapter's page list came already (its first pages are loading or loaded): for tests.
    bool nextChapterReady() const { return next_.has_value() && next_->listReady; }

  private:
    struct Slot {
        ReaderPage page;
        std::string url;  // full URL; empty: the server listed this page in a form we can't use
        net::CancelToken cancel;
    };
    // The next chapter, fetched near the end of this one.
    struct Prefetched {
        int chapterId = 0;
        bool listReady = false;
        core::PageList list;
        std::vector<Slot> slots;
    };
    // Where a chapter opens once its page list is there.
    struct Start {
        enum class Kind { Page, Resume, Last } kind = Kind::Page;
        int page = 0;
    };
    struct Saver;

    void load(int chapterId, Start start);
    void leaveChapter();
    void onPages(Start start, const api::ApiResult<core::PageList>& result);
    void showList(const core::PageList& list, std::vector<Slot> ready, Start start);
    std::vector<Slot> makeSlots(const core::PageList& list) const;
    void arrive();
    void updateWindow();
    void startLoad(Slot& slot, int chapterId, int index, net::Priority priority);
    void onLoaded(int chapterId, int index, const image::ImageResult& result);
    void reloadNetworkFailures();
    void requestNext();
    void onNextPages(int chapterId, const api::ApiResult<core::PageList>& result);
    void resetSlot(Slot& slot);
    void onSaved(const core::ReadProgress& progress);
    int chapterIndex(int chapterId) const;
    void changed();

    api::SuwayomiClient& client_;
    image::ImageLoader& images_;
    core::Manga manga_;
    std::vector<core::Chapter> chapters_;  // by sourceOrder, oldest first
    core::MangaReaderSettings settings_;
    ReaderOptions options_;
    std::function<void()> listener_;
    std::shared_ptr<Saver> saver_;

    bool opened_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    core::Chapter chapter_;
    Start start_;  // how the chapter under way opens (for retry)
    net::CancelToken listCancel_;
    std::vector<Slot> slots_;
    int position_ = 0;
    std::shared_ptr<core::ProgressSync> sync_;  // the opened chapter's progress
    std::optional<Prefetched> next_;
    net::CancelToken nextCancel_;
};

}  // namespace snx::ui
