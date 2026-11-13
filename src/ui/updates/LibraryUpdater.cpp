#include "ui/updates/LibraryUpdater.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

namespace {

// retryMs, doubled per failure in a row, at most retryMaxMs (D45's back-off).
int backoffMs(int retryMs, int failures, int retryMaxMs) {
    long long wait = std::max(retryMs, 1);
    for (int i = 1; i < failures && wait < retryMaxMs; ++i) {
        wait *= 2;
    }
    return static_cast<int>(std::min<long long>(wait, std::max(retryMaxMs, 1)));
}

}  // namespace

LibraryUpdater::LibraryUpdater(api::SuwayomiClient& client, Limits limits, Clock clock, Schedule schedule)
    : client_(client), limits_(limits), clock_(std::move(clock)), schedule_(std::move(schedule)) {}

LibraryUpdater::~LibraryUpdater() {
    cancel_.cancel();
    alive_.reset();
}

int LibraryUpdater::addListener(std::function<void()> listener) {
    const int id = nextListener_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void LibraryUpdater::removeListener(int id) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                    [id](const auto& entry) { return entry.first == id; }),
                     listeners_.end());
}

void LibraryUpdater::changed() {
    const auto copy = listeners_;  // a listener may remove itself
    for (const auto& entry : copy) {
        entry.second();
    }
}

void LibraryUpdater::check() {
    if (active() || checking_) {
        return;
    }
    checking_ = true;
    const net::CancelToken token = cancel_;
    client_.libraryUpdateStatus(false, token).then([this, token](const api::ApiResult<core::LibraryUpdateStatus>& r) {
        if (token.cancelled()) {
            return;
        }
        checking_ = false;
        if (!r.ok()) {
            pollFailed_ = true;
            pollError_ = r.error();
            changed();
            return;
        }
        pollFailed_ = false;
        status_ = r.value();
        if (r.value().running && !active()) {
            // Somebody else's update (the web UI, the server's schedule): follow it until nothing runs.
            watch_ = core::LibraryUpdateWatch::alreadyRunning();
            summary_.reset();
            stopSent_ = false;
            failures_ = 0;
            tappedAt_ = clock_();
            phase_ = Phase::Running;
            schedulePoll(limits_.pollMs);
        }
        changed();
    });
}

bool LibraryUpdater::start() {
    if (active()) {
        return false;
    }
    phase_ = Phase::Starting;
    summary_.reset();
    stopSent_ = false;
    failures_ = 0;
    pollFailed_ = false;
    tappedAt_ = clock_();
    const net::CancelToken token = cancel_;
    // The status before: its timestamp tells this update from the last one.
    client_.libraryUpdateStatus(false, token).then([this, token](const api::ApiResult<core::LibraryUpdateStatus>& before) {
        if (token.cancelled()) {
            return;
        }
        if (!before.ok()) {
            pendingError_ = before.error();
            phase_ = Phase::Idle;
            changed();
            return;
        }
        status_ = before.value();
        const core::LibraryUpdateStatus previous = before.value();
        client_.startLibraryUpdate(token).then([this, token, previous](const api::ApiResult<bool>& started) {
            if (token.cancelled()) {
                return;
            }
            if (!started.ok()) {
                pendingError_ = started.error();
                phase_ = Phase::Idle;
                changed();
                return;
            }
            watch_ = core::LibraryUpdateWatch::started(previous, clock_(), limits_.startWaitMs, limits_.pollMs);
            schedulePoll(limits_.pollMs);
            changed();
        });
    });
    changed();
    return true;
}

bool LibraryUpdater::stop() {
    if (!active() || stopSent_ || !watch_) {
        return false;
    }
    stopSent_ = true;
    watch_->stopping();
    const net::CancelToken token = cancel_;
    client_.stopLibraryUpdate(token).then([this, token](const api::ApiResult<bool>& result) {
        if (token.cancelled() || result.ok()) {
            return;
        }
        pendingError_ = result.error();
        stopSent_ = false;  // Stop can be tapped again
        changed();
    });
    changed();
    return true;
}

void LibraryUpdater::schedulePoll(int ms) {
    polling_ = true;
    const std::weak_ptr<bool> alive = alive_;
    schedule_(ms, [this, alive] {
        if (alive.expired()) {
            return;
        }
        poll();
    });
}

void LibraryUpdater::poll() {
    const net::CancelToken token = cancel_;
    client_.libraryUpdateStatus(false, token).then([this, token](const api::ApiResult<core::LibraryUpdateStatus>& r) {
        if (!token.cancelled()) {
            onPoll(r);
        }
    });
}

void LibraryUpdater::onPoll(const api::ApiResult<core::LibraryUpdateStatus>& result) {
    polling_ = false;
    if (!result.ok()) {
        // The server is away (or answered badly): say so and try again later. The update runs on the server anyway.
        pollFailed_ = true;
        pollError_ = result.error();
        failures_ += 1;
        schedulePoll(backoffMs(limits_.retryMs, failures_, limits_.retryMaxMs));
        changed();
        return;
    }
    pollFailed_ = false;
    failures_ = 0;
    status_ = result.value();
    const core::UpdatePhase next = watch_->next(result.value(), clock_());
    if (next == core::UpdatePhase::Done || next == core::UpdatePhase::NothingToUpdate) {
        finish(next);
        return;
    }
    phase_ = next == core::UpdatePhase::Running ? Phase::Running : Phase::Starting;
    schedulePoll(limits_.pollMs);
    changed();
}

void LibraryUpdater::finish(core::UpdatePhase phase) {
    auto summary = std::make_shared<Summary>();
    summary->nothingToUpdate = phase == core::UpdatePhase::NothingToUpdate;
    summary->stopped = watch_->stopped();
    summary->skippedManga = status_->skippedManga;
    summary->total = status_->total;
    const std::int64_t startedMs = status_->lastUpdateMs;
    const net::CancelToken token = cancel_;
    auto done = [this, token, summary] {
        summary->ms = clock_() - tappedAt_;
        summary_ = *summary;
        phase_ = Phase::Finished;
        stopSent_ = false;
        finished_ += 1;
        changed();
    };
    // The failed manga, then the chapters this update found (fetched since it started; the first page is enough for
    // the summary). A read that fails leaves its part of the summary empty.
    client_.libraryUpdateStatus(true, token)
        .then([this, token, summary, startedMs, done](const api::ApiResult<core::LibraryUpdateStatus>& withManga) {
            if (token.cancelled()) {
                return;
            }
            if (withManga.ok()) {
                summary->failed = withManga.value().failed;
            }
            if (summary->nothingToUpdate || startedMs <= 0) {
                done();
                return;
            }
            client_.updates(0, token).then(
                [token, summary, startedMs, done](const api::ApiResult<core::NodeList<core::ChapterEntry>>& page) {
                    if (token.cancelled()) {
                        return;
                    }
                    if (page.ok()) {
                        for (const core::ChapterEntry& entry : page.value().nodes) {
                            // fetchedAt is whole seconds, the timestamp milliseconds.
                            if (core::isNewChapter(entry) && entry.chapter.fetchedAt * 1000 >= startedMs - 999) {
                                summary->newChapters += 1;
                            }
                        }
                    }
                    done();
                });
        });
}

std::optional<api::ApiError> LibraryUpdater::takeError() {
    std::optional<api::ApiError> error = std::move(pendingError_);
    pendingError_.reset();
    return error;
}

}  // namespace snx::ui
