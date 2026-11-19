#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/ServerDownloads.hpp"
#include "net/CancelToken.hpp"

// The server's download queue as the app knows it (WP-17, D58), without borealis so it is unit-tested (FakeHttpClient, a
// played clock). One for the app (AppContext::downloads, made in main() after the client); the Server downloads tab and the
// details screen watch it, MainActivity calls reset() when the server or the login changes. It copies LibraryUpdater's
// pattern: hooks for the clock and the timers, several listeners, D45's back-off.
//
// - The queue = the last downloadStatus the server sent; every download mutation answers it too. Newest answer wins: a
//   read gets a number when it is sent, an action's answer when it arrives (it is the queue after the action, newer than
//   any read sent meanwhile: a read can catch the server halfway, e.g. a clear stopped but not emptied yet), and an answer
//   older than the last one applied is dropped (a poll that waited must not bring back an old queue).
// - Polling: downloadStatus every pollMs, only while someone watches and the queue is busy (Running or Starting). A new
//   watcher gets one read at once. A failed poll waits retryMs, doubled per failure up to retryMaxMs, while watched.
// - Chapters that leave the queue without our Remove / Clear (most likely downloaded) are reported as `left`; the chapters a
//   delete answered as `deleted`.
// - Actions are never repeated; each answer is applied by the rule above, then the polling goes on.
// - The server sometimes holds an action's answer for 30 s and then fails it ("Timed out"), although the change itself
//   went through at once (seen with reorder on v2.4.2366: it waits for news it may have sent before it listened). So for
//   actions whose result the queue shows (remove, top, bottom, pause, resume, clear): checkMs after sending, if no answer
//   came yet, one read; when it shows the action done, that is its answer (ok). A failed answer is checked the same way
//   before the error is shown.
namespace snx::ui {

class ServerDownloads {
  public:
    struct Limits {
        int pollMs = 1000;       // RuntimeProfile::serverDownloadPollMs
        int startWaitMs = 5000;  // RuntimeProfile::serverDownloadStartWaitMs
        int retryMs = 5000;      // RuntimeProfile::progressSyncMs
        int retryMaxMs = 60000;  // RuntimeProfile::progressRetryMaxMs
        int batchMax = 1000;     // RuntimeProfile::serverDownloadBatchMax
        int checkMs = 500;       // RuntimeProfile::serverDownloadCheckMs
    };
    using Clock = std::function<std::int64_t()>;                         // the app's clock, milliseconds
    using Schedule = std::function<void(int ms, std::function<void()>)>;  // run fn on the main thread after ms (brls::delay)

    enum class Action { Enqueue, Remove, Top, Bottom, Pause, Resume, Retry, RetryFailed, Clear, Delete, DeleteAfterReading };
    static const char* actionName(Action action);  // "enqueue", "remove", "top", ... (the test event)

    struct ActionDone {
        Action action = Action::Enqueue;
        int chapters = 0;  // how many the action was about
        bool ok = true;
        std::optional<api::ApiError> error;
        std::int64_t ms = 0;  // from the call to its answer, by the app's clock
    };
    // What a listener hears after each change.
    struct Change {
        bool queue = false;                 // a new queue was applied (or a poll failed / recovered)
        std::vector<int> left;              // chapters that left the queue without our Remove / Clear
        std::vector<core::Chapter> deleted; // chapters a delete answered (id + isDownloaded)
        std::optional<ActionDone> action;   // an action's answer arrived
    };
    using Listener = std::function<void(const Change&)>;

    ServerDownloads(api::SuwayomiClient& client, Limits limits, Clock clock, Schedule schedule);
    ~ServerDownloads();  // cancels what is under way; a scheduled poll then does nothing
    ServerDownloads(const ServerDownloads&) = delete;
    ServerDownloads& operator=(const ServerDownloads&) = delete;

    // Listeners run on the main thread (the client's deliver / the schedule). The int removes it again.
    int addListener(Listener listener);
    void removeListener(int id);

    // A screen that shows the queue: watch() when it comes on top (one read at once), unwatch() when it goes.
    int watch();
    void unwatch(int id);
    bool watched() const { return !watchers_.empty(); }
    void refresh();  // one read now (MINUS -> Refresh); nothing when a read is already on its way

    const std::optional<core::DownloadQueue>& queue() const { return queue_; }  // nullopt before the first answer
    core::QueueView view() const;
    bool busy() const;  // Running or Starting: polled while watched
    const core::DownloadRow* row(int chapterId) const;  // the chapter's row in the last queue, or nullptr
    std::vector<int> queuedIds() const;                  // every kept row's chapter
    bool pollFailed() const { return pollFailed_; }      // the last read found no server ("Can't reach the server")
    const api::ApiError& pollError() const { return pollError_; }
    int polls() const { return polls_; }                 // downloadStatus reads sent so far (the test event)
    std::int64_t applyMs() const { return applyMs_; }    // how long applying the last queue took (by the app's clock)
    // An action's error (empty afterwards): the view tells the user.
    std::optional<api::ApiError> takeError();

    // The actions. Each returns false when it doesn't apply (nothing sent).
    bool enqueue(const std::vector<int>& ids);  // in pieces of batchMax; empty: false
    bool removeFromQueue(int chapterId);                 // only a chapter in the last queue
    bool retry(int chapterId);                  // a failed row: queued again (the server's own way)
    bool retryFailed();                         // every failed row
    bool moveToTop(int chapterId);
    bool moveToBottom(int chapterId);
    bool pause();   // while rows wait (Running or Starting)
    bool resume();  // only with a QUEUED row (with only failed rows the tab offers Retry failed)
    bool clear();   // while the queue has rows
    // Deletes the chapters' downloads on the server (Delete server download, or `DeleteAfterReading` at Background).
    bool deleteDownloads(const std::vector<int>& ids, Action action = Action::Delete,
                         net::Priority priority = net::Priority::Interactive);

    // The server or the login changed: forget the queue, cancel what is under way, read again if watched.
    void reset();

  private:
    using QueueResult = api::ApiResult<core::DownloadQueue>;
    std::uint64_t nextSeq() { return ++sent_; }
    void read(net::Priority priority = net::Priority::Interactive);
    void updatePolling();
    void schedulePoll(int ms);
    void apply(std::uint64_t seq, const core::DownloadQueue& queue, Change& change);
    // Says whether the queue we know shows the action done (null: only the answer counts).
    using DoneCheck = std::function<bool()>;
    // Fills change.action with the action's result (no error = ok) and does what the action needs afterwards.
    using Report = std::function<void(Change& change, std::optional<api::ApiError> error)>;
    struct PendingAction {
        bool answered = false;  // the action's own answer arrived
        bool reported = false;  // its result went to the listeners (once)
    };
    bool runQueueAction(Action action, int chapters, std::function<api::ApiFuture<core::DownloadQueue>()> call,
                        DoneCheck done = {}, std::function<void(bool ok)> finished = {});
    // One read for an action without an answer yet (error empty) or with a failed one: done = ok, else the error.
    void confirmAction(const std::shared_ptr<PendingAction>& pending, const DoneCheck& done, const Report& report,
                       std::optional<api::ApiError> error);
    void notify(const Change& change);

    api::SuwayomiClient& client_;
    Limits limits_;
    Clock clock_;
    Schedule schedule_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // scheduled polls check it
    net::CancelToken cancel_;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextListener_ = 1;
    std::set<int> watchers_;
    int nextWatcher_ = 1;
    std::optional<core::DownloadQueue> queue_;
    std::set<int> expectedGone_;  // chapters our Remove / Clear took out: not reported as left
    std::uint64_t sent_ = 0;      // requests numbered when sent
    std::uint64_t applied_ = 0;   // the newest number applied
    std::int64_t startAskedAt_ = 0;
    bool reading_ = false;        // a downloadStatus read is on its way
    bool pollScheduled_ = false;
    bool pollFailed_ = false;
    api::ApiError pollError_;
    int failures_ = 0;
    int polls_ = 0;
    std::int64_t applyMs_ = 0;
    std::optional<api::ApiError> pendingError_;
};

}  // namespace snx::ui
