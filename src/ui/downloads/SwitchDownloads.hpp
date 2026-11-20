#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/Offline.hpp"
#include "core/SwitchDownloads.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "net/CancelToken.hpp"
#include "net/HttpClient.hpp"
#include "ui/downloads/DownloadStore.hpp"

// Downloads to the Switch's SD card (WP-18, D59): the queue and the downloaded chapters as the app knows them. One for the
// app (AppContext::switchDownloads, made in main() after the client and the loader); the Switch downloads tab, the details
// screen and the reader read it, MainActivity calls setServer() / connected(). No borealis: unit-tested
// (tests/unit/test_ui_switch_downloads.cpp, FakeHttpClient, a played clock and schedule, a temp folder).
//
// - One chapter at a time, in the queue's order: fetchChapterPages at Background priority (the page list and its count;
//   the server learns the count there), once per manga its details (manga.json's fields) and cover, then the pages not on
//   the SD card yet, `pagesAtOnce` at a time, as plain GETs at Background priority (the reader's pages go first). Each page
//   is checked before it is written (S6): HTTP 200, a known image format by its bytes, its header's sides and decode size.
//   No decoding. After the last page: manga.json with the chapter (the commit), then the next chapter.
// - A page that fails (an HTTP error, a check) is tried `pageTries` times in all, `retryMs` apart, doubling; still failing:
//   the chapter is Failed ("Failed at page 2 of 10"), keeps its pages, and the queue goes on. Can't reach the server
//   (Offline, Timeout): no try is counted, the queue waits (serverRetryMs doubling up to serverRetryMaxMs, or connected()).
//   A login that no longer works (Auth, a second 401): it waits for connected().
// - Space, before every page: the limit (Settings), and minSdFree always left free; free space unknown: no write. It waits
//   and goes on by itself after a delete or a higher limit. Nothing is ever deleted to make room.
// - Memory: while `memoryLow()` (the loader's prefetch pause) no new page request starts.
// - Not focused (HOME, sleep): the running requests are cancelled (those pages come again), no writes, queue.json flushed.
// - queue.json is written at most once per saveDelayMs, at once by flush().
namespace snx::ui {

class SwitchDownloads {
  public:
    struct Limits {
        int pagesAtOnce = 2;           // RuntimeProfile::switchDownloadPagesAtOnce
        int pageTries = 3;             // switchDownloadPageTries
        int retryMs = 1000;            // switchDownloadRetryMs
        int serverRetryMs = 5000;      // progressSyncMs
        int serverRetryMaxMs = 60000;  // progressRetryMaxMs
        int authRetryMs = 1000;        // imageAuthRetryMs
        std::uint64_t maxImageBytes = 32ULL * 1024 * 1024;  // maxImageBytes
        int imageTimeoutMs = 60000;    // httpImageTimeoutMs
        int maxImageSide = 16384;      // maxImageSide
        std::size_t queueMax = 1000;   // switchQueueMax
        int saveDelayMs = 2000;        // configSaveDelayMs
        std::uint64_t minSdFree = 1024ULL * 1024 * 1024;  // minSdFreeBytes
        std::uint64_t defaultLimit = 8ULL * 1024 * 1024 * 1024;  // switchDownloadLimitBytes
        int progressBatch = 500;       // downloadedProgressBatch (WP-18b)
    };
    static Limits limitsFrom(const core::RuntimeProfile& profile);

    struct Hooks {
        std::function<std::int64_t()> clockMs;                     // a steady clock (the app: appClockMs). Required
        std::function<void(int ms, std::function<void()>)> later;  // fn on the main thread after ms (brls::delay). Required
        std::function<void(std::function<void()>)> post;           // fn on the main thread soon, from any thread. Required
        std::function<std::int64_t()> nowSeconds;                 // seconds since 1970 (a chapter's downloadedAt)
        std::function<bool()> memoryLow;                           // the image loader's prefetchPaused()
        std::function<void(bool warning, const std::string& line)> log;
    };

    // client: fetchChapterPages and the manga's details; http: the pages and the cover (the client decorates it with the
    // login). Both, and the store, must outlive this.
    SwitchDownloads(api::SuwayomiClient& client, net::HttpClient& http, DownloadStore& store, Limits limits, Hooks hooks);
    ~SwitchDownloads();
    SwitchDownloads(const SwitchDownloads&) = delete;
    SwitchDownloads& operator=(const SwitchDownloads&) = delete;

    // --- The app's life ---
    // The configured server (config.json's address). A new one: the running download stops, that server's folder and
    // queue load (store.scan). The same one: nothing.
    void setServer(const std::string& url);
    // The main screen's server check worked (start-up, Retry, a new login): a queue loaded from queue.json starts, and one
    // waiting for the server or a login goes on.
    void connected();
    void setFocused(bool focused);  // HOME / sleep: false
    void setLimit(std::uint64_t bytes);  // Settings' "Space for downloads", 0 = no limit
    std::uint64_t limit() const { return limit_; }
    // The reader's "server converts to JPEG" setting: the page list (and so the files) the next chapter downloads.
    void setPageFormat(api::PageFormat format) { format_ = format; }
    // Exit (S5): the running requests are cancelled, the queue file written. Nothing runs afterwards.
    void shutdown();

    // --- What screens read ---
    bool loaded() const { return loaded_; }  // the server's folder was read
    enum class ChapterState { None, Queued, Downloading, Paused, Failed, OnSwitch, Damaged };
    struct ChapterStatus {
        ChapterState state = ChapterState::None;
        int pagesDone = 0;    // Downloading / Paused / Failed
        int pages = 0;        // 0 = not known yet
        int failedPage = -1;  // Failed: the first page that failed (0-based), -1 = the page list
    };
    ChapterStatus chapterState(int chapterId) const;
    static const char* chapterStateName(ChapterState state);  // "none", "queued", ..., "on_switch", "damaged"

    const core::SwitchQueue& queue() const { return queue_; }
    int failedCount() const;
    core::SwitchWait wait() const;
    core::SwitchQueueView view() const;
    int downloadingChapter() const { return running_ ? running_->chapterId : 0; }
    // The tab's manga rows, by title: what is on the Switch (damaged manga folders too).
    struct MangaRow {
        int mangaId = 0;
        std::string title;
        std::string thumbnailUrl;
        std::string coverFile;    // "" without a cover on the SD card
        int chapters = 0;
        int damagedChapters = 0;
        std::uint64_t bytes = 0;
        bool damaged = false;     // its manga.json can't be read: only "Delete all downloads..." works
    };
    std::vector<MangaRow> downloadedManga() const;
    const core::SwitchManga* manga(int mangaId) const;  // its index, nullptr if nothing of it is on the Switch
    // The reader: one file per page ("" for a page the server listed in a form the app can't use), or nothing when the
    // chapter isn't on the Switch (a queued one isn't).
    std::optional<std::vector<std::string>> pageFiles(int chapterId) const;
    // The chapters on this Switch or queued here (the bulk choices' skip set).
    std::set<int> skipIds() const;
    struct Space {
        std::uint64_t used = 0;
        std::uint64_t limit = 0;  // 0 = no limit
        std::optional<std::uint64_t> free;
    };
    Space space() const;
    int queueWrites() const { return store_.queueWrites(); }
    int freeMs() const { return freeMs_; }  // how long the last freeBytes() took
    std::optional<std::string> takeError();  // an action's error, once ("Can't write to the SD card.")

    // --- Actions (main thread; at once, nothing to wait for) ---
    enum class Action { Enqueue, Remove, Top, Bottom, Pause, Resume, Retry, RetryFailed, Clear, Delete, DeleteManga,
                        DeleteAfterReading };
    static const char* actionName(Action action);
    // Queues the chapters (reading order is the caller's) that aren't on the Switch or queued, up to queueMax in all.
    // Returns how many went in; `capped` says some didn't fit.
    int enqueue(const core::Manga& manga, const std::vector<core::Chapter>& chapters, bool* capped = nullptr);
    bool removeFromQueue(int chapterId);  // a downloading one stops; its pages go
    bool retry(int chapterId);            // a failed entry, or a damaged chapter (its broken pages come again)
    bool retryFailed();
    bool moveToTop(int chapterId);
    bool moveToBottom(int chapterId);
    bool pause();
    bool resume();
    bool clear();  // every queued and failed entry and their pages; downloaded chapters stay
    bool deleteChapters(const std::vector<int>& ids, Action action = Action::Delete);
    bool deleteManga(int mangaId);  // its chapters, its queued entries, its folder (a damaged one too)
    // The reader found a page missing, cut short or undecodable: the chapter shows "damaged" (saved in manga.json).
    void markDamaged(int chapterId, int page);

    // --- What the Switch knows of its downloaded chapters' progress (WP-18b, D61): progress.json ---
    // The server's progress of a downloaded chapter as last seen (and whether the server still has it). nullptr: nothing.
    const core::KnownProgress* knownProgress(int chapterId) const;
    // The server's progress of a chapter: the download's page list, the reader's chapterState and saved answers, a waiting
    // save's round. Only chapters on this Switch; the file is written (debounced like queue.json) only when it changed.
    // full: the bookmark and the strip's note are known too (else the note is kept unless `server` has one).
    void noteProgress(int chapterId, const core::ServerProgress& server, bool full);
    // A gone chapter read here: its progress lives only in progress.json (answer 8).
    void noteLocal(int chapterId, const core::LocalProgress& local);
    // A details screen's complete chapter list (under maxChapters): each listed downloaded chapter's progress; a downloaded
    // chapter of the manga it doesn't list is gone.
    void noteMangaChapters(int mangaId, const std::vector<core::Chapter>& chapters);
    // After a check that worked: DownloadedProgress for every downloaded chapter of this server, `progressBatch` ids per
    // request at Background priority, one after the other; an id missing from an answer is gone. One at a time (a call while
    // one runs runs again after it). The listeners hear Change::refreshed.
    void refreshProgress();
    bool refreshingProgress() const { return refreshing_; }
    int progressWrites() const { return store_.progressWrites(); }

    // --- "Same server?" (WP-18c, D59): the setup screen saves a new address while the saved server is still loaded here ---
    struct AddressQuestion {
        bool ask = false;
        std::string oldUrl;  // the saved server's address (downloadServerUrl form)
        std::string newUrl;  // the address about to be saved (same form)
        int chapters = 0;    // the saved server's downloaded chapters
    };
    // Ask only when the saved server has downloaded chapters and no server.json on the SD card lists newUrl yet. done runs
    // on the main thread: at once, or after a look at the SD card (the store's thread).
    void addressQuestion(const std::string& newUrl, std::function<void(AddressQuestion)> done);
    // Same server: newUrl joins the saved server's server.json (at most kSwitchUrlsMax, core::addServerUrl), written before
    // the scan of the next setServer(). Another server: newUrl gets a folder of its own with only its server.json, so the
    // question comes once per address. A failed write: done(false) and takeError().
    void joinAddress(const std::string& newUrl, std::function<void(bool ok)> done = {});
    void keepApart(const std::string& newUrl, std::function<void(bool ok)> done = {});

    // --- Listeners (main thread) ---
    struct Change {
        bool state = false;  // anything a screen shows (queue, progress, the index, space, the view)
        std::optional<Action> action;  // an action ran
        int actionChapters = 0;
        bool actionOk = true;
        std::string actionError;
        // A chapter finished: committed (ok) or failed.
        struct Finished {
            int chapterId = 0;
            bool ok = false;
            int pages = 0;
            std::uint64_t bytes = 0;
            std::int64_t ms = 0;
            std::string error;
        };
        std::optional<Finished> finished;
        // WP-18b: a progress refresh ended (the "downloaded_progress" test event).
        struct Refreshed {
            int chapters = 0;  // downloaded chapters asked for
            int gone = 0;      // of them, the server no longer has
            int changed = 0;   // entries that changed
            std::int64_t ms = 0;
            bool ok = true;    // false: a request failed, the rest wasn't asked
            int requests = 0;
        };
        std::optional<Refreshed> refreshed;
    };
    using Listener = std::function<void(const Change&)>;
    int addListener(Listener listener);
    void removeListener(int id);

  private:
    struct Page {
        char format = core::kPageJpeg;
        std::uint64_t bytes = 0;
        bool done = false;
        bool asked = false;  // a request or a write is under way
        int tries = 0;
        bool authRetried = false;
    };
    struct Running {
        int chapterId = 0;
        int mangaId = 0;
        std::uint64_t run = 0;          // the run number it belongs to
        std::int64_t startedMs = 0;
        bool listed = false;            // the page list came
        std::vector<std::string> urls;  // full URLs, "" for an unusable page
        std::vector<Page> pages;
        int inFlight = 0;
        bool committing = false;
        core::Chapter serverChapter;         // the page list's chapter: the server's progress for progress.json (WP-18b)
    };

    void say(bool warning, const std::string& line) const;
    void notify(const Change& change);
    void changed();       // something a screen shows changed: the listeners hear it
    void queueChanged();  // the queue changed: queue.json is written soon (at most once per saveDelayMs)
    void stopRunning();   // cancels the running chapter's requests (its pages stay)
    void kick();          // starts the next chapter, or the next pages, when nothing holds the queue
    bool holdsBack() const;
    bool current(std::uint64_t run) const { return running_ && running_->run == run && run == run_; }
    void startChapter(const core::SwitchQueueEntry& entry);
    void afterManga(std::uint64_t run);
    void requestPages(std::uint64_t run);
    void onPageList(std::uint64_t run, const api::ApiResult<core::PageList>& result);
    void onListed(std::uint64_t run, const std::map<int, DownloadStore::PageFile>& onCard);
    void pump(std::uint64_t run);
    void fetchPage(std::uint64_t run, int index);
    void onPage(std::uint64_t run, int index, const net::HttpResult& result);
    void pageFailed(std::uint64_t run, int index, const std::string& why);
    void commit(std::uint64_t run);
    void failChapter(int chapterId, int failedPage, const std::string& why);
    void serverAway(bool login);
    void scheduleKick(int ms);
    std::uint64_t used() const;
    std::optional<std::uint64_t> freeNow() const;
    bool spaceFor(std::uint64_t bytes);  // false: the queue waits (wait_ says why)
    core::SwitchQueueEntry* entry(int chapterId);
    const core::SwitchQueueEntry* entry(int chapterId) const;
    core::SwitchManga* mangaOf(int mangaId);
    const core::SwitchManga* mangaOf(int mangaId) const;
    bool anyQueuedFor(int mangaId) const;
    void ensureFolder();
    void dropMangaIfEmpty(int mangaId);
    void putManga(core::SwitchManga manga);    // into manga_ (replaces)
    void putPending(core::SwitchManga manga);  // into pendingManga_, with a cover from before a restart
    void measureFree();
    std::string pagePath(int mangaId, int chapterId, int index, char format) const;
    void report(Action action, int chapters, bool ok, const std::string& error = {});
    // WP-18b: progress.json.
    struct Refresh {
        std::vector<int> ids;
        std::size_t next = 0;
        int gone = 0;
        int changed = 0;
        int requests = 0;
        std::int64_t startedMs = 0;
    };
    const core::SwitchChapter* downloadedChapter(int chapterId) const;
    bool learn(int chapterId, const core::ServerProgress& server, bool full);  // true if the entry changed
    bool markGone(int chapterId);  // true if it wasn't gone before
    void progressChanged();       // progress.json is written soon (at most once per saveDelayMs)
    void refreshStep(Refresh refresh);
    void endRefresh(const Refresh& refresh, bool ok);

    api::SuwayomiClient& client_;
    net::HttpClient& http_;
    DownloadStore& store_;
    Limits limits_;
    Hooks hooks_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    std::string url_;            // the configured server
    std::string key_;            // its folder
    std::vector<std::string> urls_;  // its server.json
    bool serverWritten_ = false;     // server.json written in this run (before the folder's first file)
    bool loaded_ = false;
    api::PageFormat format_ = api::PageFormat::AsStored;
    std::uint64_t scanTicket_ = 0;
    bool connected_ = false;
    bool focused_ = true;
    bool stopped_ = false;
    std::uint64_t limit_ = 0;

    core::SwitchQueue queue_;
    std::vector<core::SwitchManga> manga_;   // the indexes, as written
    std::vector<int> damagedManga_;          // manga folders whose index can't be read
    std::map<int, std::uint64_t> partial_;   // queued chapters' bytes on the SD card so far
    std::set<int> coverTried_;               // manga whose cover was asked for this run of the app
    std::map<int, core::SwitchManga> pendingManga_;  // details fetched for a manga with nothing committed yet
    std::map<int, std::uint64_t> covers_;  // a queued manga's cover.img from before a restart (no manga.json yet)
    std::map<int, std::pair<int, int>> progress_;    // queued chapters: pages on the SD card, of how many (kept when paused)

    std::optional<Running> running_;
    std::uint64_t run_ = 0;                  // bumped by every stop: late answers of an older run are ignored
    net::CancelToken cancel_;                // the running chapter's requests
    core::SwitchWait wait_ = core::SwitchWait::None;
    int serverFailures_ = 0;
    std::uint64_t kickTicket_ = 0;           // only the newest timer kicks

    std::optional<std::uint64_t> freeMeasured_;  // the SD card's free bytes when last asked ...
    std::uint64_t writtenSince_ = 0;             // ... and what was written since
    int freeMs_ = 0;
    bool queueWriteScheduled_ = false;
    core::SwitchProgress known_;         // WP-18b: progress.json, only chapters on this Switch
    bool progressWriteScheduled_ = false;
    bool refreshing_ = false;
    bool refreshAgain_ = false;
    net::CancelToken refreshCancel_;
    std::optional<std::string> pendingError_;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextListener_ = 1;
};

}  // namespace snx::ui
