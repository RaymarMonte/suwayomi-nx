#include "ui/downloads/ServerDownloads.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

namespace {

// retryMs, doubled per failure in a row, at most retryMaxMs (D45's back-off, as LibraryUpdater).
int backoffMs(int retryMs, int failures, int retryMaxMs) {
    long long wait = std::max(retryMs, 1);
    for (int i = 1; i < failures && wait < retryMaxMs; ++i) {
        wait *= 2;
    }
    return static_cast<int>(std::min<long long>(wait, std::max(retryMaxMs, 1)));
}

}  // namespace

const char* ServerDownloads::actionName(Action action) {
    switch (action) {
        case Action::Enqueue: return "enqueue";
        case Action::Remove: return "remove";
        case Action::Top: return "top";
        case Action::Bottom: return "bottom";
        case Action::Pause: return "pause";
        case Action::Resume: return "resume";
        case Action::Retry: return "retry";
        case Action::RetryFailed: return "retry_failed";
        case Action::Clear: return "clear";
        case Action::Delete: return "delete";
        case Action::DeleteAfterReading: return "delete_after_reading";
    }
    return "enqueue";
}

ServerDownloads::ServerDownloads(api::SuwayomiClient& client, Limits limits, Clock clock, Schedule schedule)
    : client_(client), limits_(limits), clock_(std::move(clock)), schedule_(std::move(schedule)) {}

ServerDownloads::~ServerDownloads() {
    cancel_.cancel();
    alive_.reset();
}

int ServerDownloads::addListener(Listener listener) {
    const int id = nextListener_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void ServerDownloads::removeListener(int id) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [id](const auto& entry) { return entry.first == id; }),
                     listeners_.end());
}

void ServerDownloads::notify(const Change& change) {
    const auto copy = listeners_;  // a listener may remove itself
    for (const auto& entry : copy) {
        entry.second(change);
    }
}

int ServerDownloads::watch() {
    const int id = nextWatcher_++;
    watchers_.insert(id);
    read();  // one read at once: a queue started elsewhere (the web UI) shows too
    return id;
}

void ServerDownloads::unwatch(int id) {
    watchers_.erase(id);  // a scheduled poll then finds nobody watching and stops
}

void ServerDownloads::refresh() {
    read();
}

core::QueueView ServerDownloads::view() const {
    if (!queue_) {
        return core::QueueView::Idle;
    }
    return core::queueView(*queue_, clock_(), startAskedAt_, limits_.startWaitMs);
}

bool ServerDownloads::busy() const {
    const core::QueueView v = view();
    return v == core::QueueView::Running || v == core::QueueView::Starting;
}

const core::DownloadRow* ServerDownloads::row(int chapterId) const {
    if (!queue_) {
        return nullptr;
    }
    for (const core::DownloadRow& r : queue_->rows) {
        if (r.chapterId == chapterId) {
            return &r;
        }
    }
    return nullptr;
}

std::vector<int> ServerDownloads::queuedIds() const {
    std::vector<int> ids;
    if (queue_) {
        for (const core::DownloadRow& r : queue_->rows) {
            ids.push_back(r.chapterId);
        }
    }
    return ids;
}

std::optional<api::ApiError> ServerDownloads::takeError() {
    std::optional<api::ApiError> error = std::move(pendingError_);
    pendingError_.reset();
    return error;
}

void ServerDownloads::read(net::Priority priority) {
    if (reading_) {
        return;
    }
    reading_ = true;
    polls_ += 1;
    const std::uint64_t seq = nextSeq();
    const net::CancelToken token = cancel_;
    client_.downloadStatus(token, priority).then([this, token, seq](const QueueResult& r) {
        if (token.cancelled()) {
            return;
        }
        reading_ = false;
        Change change;
        change.queue = true;
        if (!r.ok()) {
            // The server is away (or answered badly): say so and try again later while watched. The server downloads anyway.
            pollFailed_ = true;
            pollError_ = r.error();
            failures_ += 1;
        } else {
            pollFailed_ = false;
            failures_ = 0;
            apply(seq, r.value(), change);
        }
        notify(change);
        updatePolling();
    });
}

void ServerDownloads::updatePolling() {
    if (pollScheduled_ || reading_ || !watched()) {
        return;
    }
    if (pollFailed_) {
        schedulePoll(backoffMs(limits_.retryMs, failures_, limits_.retryMaxMs));
    } else if (busy()) {
        schedulePoll(limits_.pollMs);
    }
}

void ServerDownloads::schedulePoll(int ms) {
    pollScheduled_ = true;
    const std::weak_ptr<bool> alive = alive_;
    schedule_(ms, [this, alive] {
        if (alive.expired()) {
            return;
        }
        pollScheduled_ = false;
        if (watched() && (pollFailed_ || busy())) {
            read(net::Priority::Prefetch);  // after what the user looks at (covers, actions)
        }
    });
}

void ServerDownloads::apply(std::uint64_t seq, const core::DownloadQueue& queue, Change& change) {
    if (seq < applied_) {
        return;  // an older answer than the one shown: dropped
    }
    applied_ = seq;
    const std::int64_t start = clock_();
    if (queue_) {
        for (int id : core::chaptersThatLeft(*queue_, queue)) {
            if (expectedGone_.count(id) == 0) {
                change.left.push_back(id);
            }
        }
    }
    queue_ = queue;
    // Chapters we took out are gone now: forget them (a later download of the same chapter reports "left" again).
    for (auto it = expectedGone_.begin(); it != expectedGone_.end();) {
        it = row(*it) ? std::next(it) : expectedGone_.erase(it);
    }
    applyMs_ = clock_() - start;
    change.queue = true;
}

bool ServerDownloads::runQueueAction(Action action, int chapters, std::function<api::ApiFuture<core::DownloadQueue>()> call,
                                     DoneCheck done, std::function<void(bool ok)> finished) {
    const std::int64_t tapped = clock_();
    const net::CancelToken token = cancel_;
    auto pending = std::make_shared<PendingAction>();
    const Report report = [this, action, chapters, tapped, finished](Change& change, std::optional<api::ApiError> error) {
        ActionDone result;
        result.action = action;
        result.chapters = chapters;
        result.ok = !error.has_value();
        result.error = error;
        result.ms = clock_() - tapped;
        if (error) {
            pendingError_ = error;
        }
        change.action = result;
        if (finished) {
            finished(result.ok);
        }
    };
    call().then([this, token, pending, done, report](const QueueResult& r) {
        if (token.cancelled()) {
            return;
        }
        pending->answered = true;
        Change change;
        if (r.ok()) {
            apply(nextSeq(), r.value(), change);  // numbered on arrival: the queue after the action wins over reads sent meanwhile
        }
        if (!pending->reported) {
            if (!r.ok() && done) {
                confirmAction(pending, done, report, r.error());  // it may have gone through anyway
                return;
            }
            pending->reported = true;
            report(change, r.ok() ? std::nullopt : std::optional<api::ApiError>(r.error()));
        }
        notify(change);
        updatePolling();
    });
    if (done) {
        const std::weak_ptr<bool> alive = alive_;
        schedule_(limits_.checkMs, [this, alive, token, pending, done, report] {
            if (alive.expired() || token.cancelled() || pending->answered || pending->reported) {
                return;
            }
            confirmAction(pending, done, report, std::nullopt);
        });
    }
    return true;
}

void ServerDownloads::confirmAction(const std::shared_ptr<PendingAction>& pending, const DoneCheck& done,
                                    const Report& report, std::optional<api::ApiError> error) {
    polls_ += 1;
    const std::uint64_t seq = nextSeq();
    const net::CancelToken token = cancel_;
    client_.downloadStatus(token).then([this, token, seq, pending, done, report, error](const QueueResult& r) {
        if (token.cancelled()) {
            return;
        }
        Change change;
        if (r.ok()) {
            apply(seq, r.value(), change);
        }
        if (!pending->reported) {
            if (r.ok() && done()) {
                pending->reported = true;
                report(change, std::nullopt);
            } else if (error) {
                pending->reported = true;
                report(change, error);
            }
            // An early check that doesn't show it yet: the action's own answer decides.
        }
        notify(change);
        updatePolling();
    });
}

bool ServerDownloads::enqueue(const std::vector<int>& ids) {
    if (ids.empty()) {
        return false;
    }
    startAskedAt_ = clock_();  // the answer still says STOPPED: "Starting..." until the server has begun
    for (const std::vector<int>& batch : core::idBatches(ids, limits_.batchMax)) {
        for (int id : batch) {
            expectedGone_.erase(id);
        }
        runQueueAction(Action::Enqueue, static_cast<int>(batch.size()),
                       [this, batch, token = cancel_] { return client_.enqueueDownloads(batch, token); });
    }
    return true;
}

bool ServerDownloads::retry(int chapterId) {
    const core::DownloadRow* r = row(chapterId);
    if (!r || r->state != core::DownloadState::Error) {
        return false;
    }
    startAskedAt_ = clock_();
    return runQueueAction(Action::Retry, 1,
                          [this, chapterId, token = cancel_] { return client_.enqueueDownloads({chapterId}, token); });
}

bool ServerDownloads::retryFailed() {
    std::vector<int> ids;
    if (queue_) {
        for (const core::DownloadRow& r : queue_->rows) {
            if (r.state == core::DownloadState::Error) {
                ids.push_back(r.chapterId);
            }
        }
    }
    if (ids.empty()) {
        return false;
    }
    startAskedAt_ = clock_();
    for (const std::vector<int>& batch : core::idBatches(ids, limits_.batchMax)) {
        runQueueAction(Action::RetryFailed, static_cast<int>(batch.size()),
                       [this, batch, token = cancel_] { return client_.enqueueDownloads(batch, token); });
    }
    return true;
}

bool ServerDownloads::removeFromQueue(int chapterId) {
    if (!row(chapterId)) {
        return false;  // the server would wait 30 s and fail for a chapter that isn't queued
    }
    expectedGone_.insert(chapterId);
    // It may finish meanwhile (the server then fails after 30 s): gone from the queue counts as done, quietly.
    return runQueueAction(
        Action::Remove, 1, [this, chapterId, token = cancel_] { return client_.dequeueDownload(chapterId, token); },
        [this, chapterId] { return queue_ && !row(chapterId); },
        [this, chapterId](bool ok) {
            if (!ok) {
                expectedGone_.erase(chapterId);
            }
        });
}

bool ServerDownloads::moveToTop(int chapterId) {
    if (!row(chapterId)) {
        return false;
    }
    return runQueueAction(
        Action::Top, 1, [this, chapterId, token = cancel_] { return client_.reorderDownload(chapterId, 0, token); },
        [this, chapterId] { return queue_ && !queue_->rows.empty() && queue_->rows.front().chapterId == chapterId; });
}

bool ServerDownloads::moveToBottom(int chapterId) {
    if (!row(chapterId)) {
        return false;
    }
    const int last = std::max(0, queue_->total() - 1);
    return runQueueAction(
        Action::Bottom, 1,
        [this, chapterId, last, token = cancel_] { return client_.reorderDownload(chapterId, last, token); },
        [this, chapterId] {
            return queue_ && queue_->leftOut == 0 && !queue_->rows.empty() && queue_->rows.back().chapterId == chapterId;
        });
}

bool ServerDownloads::pause() {
    if (!busy()) {
        return false;
    }
    startAskedAt_ = 0;
    return runQueueAction(
        Action::Pause, queue_->total(), [this, token = cancel_] { return client_.stopDownloads(token); },
        [this] { return queue_ && !queue_->started; });
}

bool ServerDownloads::resume() {
    // Only with a QUEUED row: with nothing queued the server waits 30 s and fails.
    if (!queue_ || queue_->queued == 0 || queue_->started) {
        return false;
    }
    startAskedAt_ = clock_();
    runQueueAction(
        Action::Resume, queue_->total(), [this, token = cancel_] { return client_.startDownloads(token); },
        [this] { return queue_ && queue_->started; });
    Change change;
    change.queue = true;  // shows "Starting..." at once (the answer takes about a second)
    notify(change);
    return true;
}

bool ServerDownloads::clear() {
    if (!queue_ || queue_->total() == 0) {
        return false;
    }
    std::vector<int> ids;
    for (const core::DownloadRow& r : queue_->rows) {
        expectedGone_.insert(r.chapterId);
        ids.push_back(r.chapterId);
    }
    startAskedAt_ = 0;
    return runQueueAction(
        Action::Clear, queue_->total(), [this, token = cancel_] { return client_.clearDownloads(token); },
        [this] { return queue_ && queue_->total() == 0; },
        [this, ids](bool ok) {
            if (!ok) {
                for (int id : ids) {
                    expectedGone_.erase(id);
                }
            }
        });
}

bool ServerDownloads::deleteDownloads(const std::vector<int>& ids, Action action, net::Priority priority) {
    if (ids.empty()) {
        return false;
    }
    const net::CancelToken token = cancel_;
    for (const std::vector<int>& batch : core::idBatches(ids, limits_.batchMax)) {
        const std::int64_t tapped = clock_();
        const int count = static_cast<int>(batch.size());
        client_.deleteServerDownloads(batch, token, priority)
            .then([this, token, action, count, tapped](const api::ApiResult<std::vector<core::Chapter>>& r) {
                if (token.cancelled()) {
                    return;
                }
                Change change;
                ActionDone done;
                done.action = action;
                done.chapters = count;
                done.ok = r.ok();
                if (r.ok()) {
                    change.deleted = r.value();
                } else {
                    done.error = r.error();
                    if (action != Action::DeleteAfterReading) {  // that one is only logged: the download stays
                        pendingError_ = r.error();
                    }
                }
                done.ms = clock_() - tapped;
                change.action = done;
                notify(change);
            });
    }
    return true;
}

void ServerDownloads::reset() {
    cancel_.cancel();
    cancel_ = net::CancelToken();
    alive_ = std::make_shared<bool>(true);  // polls scheduled before now find their old flag gone
    queue_.reset();
    expectedGone_.clear();
    applied_ = sent_;
    startAskedAt_ = 0;
    reading_ = false;
    pollScheduled_ = false;
    pollFailed_ = false;
    failures_ = 0;
    pendingError_.reset();
    Change change;
    change.queue = true;
    notify(change);
    if (watched()) {
        read();
    }
}

}  // namespace snx::ui
