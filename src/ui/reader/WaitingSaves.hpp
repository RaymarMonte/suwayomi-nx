#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/ProgressQueue.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/SaveScheduler.hpp"
#include "core/models/ReadProgress.hpp"
#include "net/CancelToken.hpp"
#include "platform/SafeFs.hpp"

// The waiting-save queue in the app (WP-15b, D53; the rules and the file format: core/ProgressQueue.hpp).
//
// - In: a progress save that fails after the reader began closing (ProgressSaver hands it over). One entry per chapter.
// - The file: progress-queue.json in the app's folder, written like config.json (SafeFs::writeAtomic + .bak, debounced by
//   core::SaveScheduler, not in a frame in which the reader moves, at once at HOME / sleep / exit: flush()). Read at start
//   (load()), the .bak if the file is broken, else empty.
// - Out, in rounds: the entries of the configured server and user, oldest first, one at a time. For each: ChapterState;
//   the server already has ours -> done ("already"); it is still at the entry's base -> setProgress -> done ("sent", the
//   listeners hear it); it moved on -> D61 decides: ours finished, or ours later by the server's clock -> sent; else
//   dropped ("server_later"). Held entries (a reader has the chapter open, WP-18b) are skipped. Can't reach the server
//   (Offline, Timeout): the entry stays and the round ends; the next one starts retryMs later, doubling up to retryMaxMs.
//   Auth: the round ends until connected() (a login worked). Other errors: the entry stays; the third in a row drops it.
// - A round starts after connected() (the main screen's server check worked), when the server answered anything over HTTP
//   (serverAnswered(), from the network thread) and no round started within retryMs, and after the back-off above.
// - The reader takes an entry over when it opens that chapter (takeOver()).
// Main thread only, except serverAnswered(). No borealis: unit-tested (tests/unit/test_ui_queue.cpp).
namespace snx::ui {

class WaitingSaves {
  public:
    struct Options {
        std::size_t maxEntries = 500;       // RuntimeProfile::pendingSavesMax
        std::size_t fileBytes = 256 * 1024;  // RuntimeProfile::progressQueueFileBytes
        int maxDepth = 64;                  // RuntimeProfile::json.maxDepth
        int saveDelayMs = 2000;             // RuntimeProfile::configSaveDelayMs
        int retryMs = 5000;                 // RuntimeProfile::progressSyncMs
        int retryMaxMs = 60000;             // RuntimeProfile::progressRetryMaxMs
    };
    static Options optionsFrom(const core::RuntimeProfile& profile);

    // What the app gives it. Every one is required except log and report.
    struct Hooks {
        std::function<std::int64_t()> clockMs;                      // a steady clock (the app: appClockMs)
        std::function<std::int64_t()> nowSeconds;                   // seconds since 1970, for an entry's madeAt (the log)
        std::function<void(int ms, std::function<void()>)> later;   // runs fn on the main thread after ms (brls::delay)
        std::function<void(std::function<void()>)> post;            // runs fn on the main thread soon, from any thread (brls::sync)
        std::function<void(bool warning, const std::string& line)> log;
        // Every change, for the "progress_queue" test event: what (added, sent, already, dropped, kept, taken, saved), the
        // entry, why (sent: send / finished / later; dropped: server_later / full / errors; kept: the error), how many
        // entries wait now, and whether a reader holds the chapter (WP-18b).
        std::function<void(const char* what, const core::WaitingSave& entry, const std::string& reason,
                           std::size_t waiting, bool held)> report;
        // WP-18b (D61). The server's clock minus the console's, in seconds (ui::Connection, from the answers' Date header):
        // the "later reading wins" rule compares through it. Empty: 0.
        std::function<std::int64_t()> clockOffset;
        // Whether the app is online (ui::Connection): no round starts while it says false; connected() starts one once the
        // server is back. Empty: always.
        std::function<bool()> online;
        // What a round learned of a chapter on the server: its state (full: page, read flag, stamp, bookmark, spot note)
        // or a save's answer (not full: page, read flag, stamp, and the spot sent). The Switch's progress.json keeps it for
        // downloaded chapters. Empty: nobody listens.
        std::function<void(int chapterId, const core::ServerProgress& server, bool full)> serverProgress;
        // WP-18b (D61): runs the file write on a worker thread (the console measured a 500-entry write at 51 ms on the
        // main thread). The text is made on the main thread; onFrame() takes the result. Empty: written at once (tests).
        std::function<void(std::function<void()>)> writeOffThread;
    };

    // fs: the app's folder (nullptr: no file, for tests that don't need one). The client must outlive this.
    WaitingSaves(api::SuwayomiClient& client, const platform::SafeFs* fs, Options options, Hooks hooks);
    ~WaitingSaves();
    WaitingSaves(const WaitingSaves&) = delete;
    WaitingSaves& operator=(const WaitingSaves&) = delete;

    static constexpr const char* kFile = "progress-queue.json";
    static constexpr const char* kBackupFile = "progress-queue.json.bak";  // SafeFs::writeAtomic's Backup::Keep name

    void load();
    // Once per main-loop iteration: writes the file when the debounce is over. readerMoving: the reader moved this frame
    // (ConfigStore::postponeSaveThisFrame was called), so a due write waits for a still frame.
    void onFrame(bool readerMoving);
    bool flush();  // writes now if anything changed; true if nothing needed writing or it worked

    // A save that failed as the reader closed; WP-18b also every save the reader makes offline, and a held copy of one that
    // failed online for want of the server. Server and user are the client's now; madeAt is set here.
    void add(core::WaitingSave entry);

    // The reader opened `chapterId` and the server answered `opened` (fetchChapterPages): if an entry of the configured
    // server waits for it, it leaves the queue. The decision (D61) sends it (still at its base, finished, or ours later):
    // the entry is returned, and the reader opens at its page and spot and saves it as its own ("taken"). The server has it
    // already: nothing ("already"). The server's reading is the later one: nothing ("dropped", "server_later").
    std::optional<core::WaitingSave> takeOver(int chapterId, const core::ServerProgress& opened);

    // WP-18b (D61), held entries: while a reader has a chapter open, its entry is held (in memory only): rounds skip it, so
    // the reader is the only one that changes it. The reader holds every chapter it opens and releases them as it closes
    // (after its last saves). After a restart nothing is held. removeHeld: the reader's save of a held chapter worked (the
    // held copy of a save that failed online goes).
    void hold(int chapterId);
    void release(int chapterId);
    bool held(int chapterId) const { return held_.count(chapterId) > 0; }
    void removeHeld(int chapterId);
    std::size_t heldCount() const { return held_.size(); }

    // The entry waiting for `chapterId` of the configured server, if any.
    const core::WaitingSave* waitingFor(int chapterId) const;
    // The newest entry of the configured server for any chapter of `mangaId` (History's "page N not saved yet": one row
    // per manga, and the chapter read offline may not be the one the server's history shows).
    const core::WaitingSave* waitingForManga(int mangaId) const;
    std::size_t size() const { return entries_.size(); }
    bool roundRunning() const { return running_; }

    // The main screen's server check worked (start-up, Retry, a changed server or login): a round starts now, also after
    // an Auth stop.
    void connected();
    // The server answered something over HTTP. Any thread (net's response observer): it only posts a nudge to the main
    // thread, and only while entries wait.
    void serverAnswered();

    // What a listener hears. Added: a save went in (History shows its note). Gone: a round sent it, or found the server
    // had it already, or dropped it (the screens showing it reload).
    enum class Change { Added, Gone };
    using Listener = std::function<void(Change, const core::WaitingSave&)>;
    // Called on the main thread. Returns an id for removal.
    int addListener(Listener listener);
    void removeListener(int id);

  private:
    struct Round {
        std::string url;
        std::string user;
        std::vector<int> chapterIds;  // the entries it takes, oldest first (looked up again at each step)
        std::size_t next = 0;
        std::string reason;  // the decision that sends the entry under way ("send", "finished", "later")
    };

    void startRound(const char* why);
    void step();
    void onState(int chapterId, const api::ApiResult<core::ServerProgress>& result);
    void onSent(int chapterId, const api::ApiResult<core::ReadProgress>& result);
    // An error for the round's entry: true if the round goes on with the next entry.
    bool failed(int chapterId, const api::ApiError& error, const char* stage);
    void endRound(bool serverAway);
    void scheduleRetry();
    core::WaitingSave* find(const std::string& url, const std::string& user, int chapterId);
    void forget(const core::WaitingSave* entry);
    void changed();
    bool save();
    // WP-18b: a write on the worker. finishWrite (main thread): takes its result once it ended; waitForWrite: blocks until
    // it ended (flush at HOME / sleep / exit, the destructor).
    struct Write {
        std::atomic<bool> done{false};
        bool ok = false;
        std::int64_t ms = 0;
        std::size_t entries = 0;
        std::size_t bytes = 0;
    };
    void finishWrite();
    void waitForWrite() const;
    bool writeNow(const std::string& text, std::int64_t started);
    void say(bool warning, const std::string& line) const;
    void tell(const char* what, const core::WaitingSave& entry, const std::string& reason) const;
    void notify(Change change, const core::WaitingSave& entry);  // the listeners (a copy: one may remove itself)
    void waitingChanged();
    std::int64_t offset() const;  // hooks_.clockOffset(), 0 without it

    api::SuwayomiClient& client_;
    const platform::SafeFs* fs_;
    Options options_;
    Hooks hooks_;
    std::vector<core::WaitingSave> entries_;  // oldest first
    std::multiset<int> held_;  // chapters a reader has open (the configured server's), once per hold (paged + strip)
    core::SaveScheduler scheduler_;
    std::shared_ptr<Write> writing_;  // the write under way on the worker, if any
    std::optional<Round> round_;
    bool running_ = false;
    bool authStopped_ = false;          // a round ended on Auth: no more rounds until connected()
    int awayRounds_ = 0;                // rounds in a row that ended for want of the server
    std::int64_t lastRoundMs_ = 0;
    bool anyRound_ = false;             // a round started since this was made
    std::uint64_t retryTicket_ = 0;     // only the latest back-off timer starts a round
    std::vector<std::pair<int, Listener>> listeners_;
    int nextListener_ = 1;
    net::CancelToken never_;            // the round's requests are never cancelled (a save cut off half way, D44)
    // serverAnswered() runs on the network thread: it reads these two only.
    std::atomic<bool> nudgeWanted_{false};  // entries of the configured server wait and no Auth stop holds them
    std::atomic<bool> nudgePosted_{false};  // a nudge is on its way to the main thread
    // Set to false when this goes: callbacks that arrive later (answers, timers, posts) check it.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace snx::ui
