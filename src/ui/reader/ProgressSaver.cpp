#include "ui/reader/ProgressSaver.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

ProgressSaver::ProgressSaver(api::SuwayomiClient& c, std::function<std::int64_t()> time,
                             std::function<void(const core::ReadProgress&, bool)> saved)
    : client(c), clock(std::move(time)), onSaved(std::move(saved)) {}

bool ProgressSaver::serverAway(const api::ApiError& error) {
    return error.kind == api::ApiErrorKind::Offline || error.kind == api::ApiErrorKind::Timeout ||
           error.httpStatus == 502 || error.httpStatus == 503 || error.httpStatus == 504;
}

void ProgressSaver::hold(int chapterId) {
    if (queue != nullptr && holding.insert(chapterId).second) {
        queue->hold(chapterId);
    }
}

core::WaitingSave ProgressSaver::entryFor(int chapterId, const core::ReadProgress& progress,
                                          const core::ServerProgress& base) const {
    core::WaitingSave entry;
    entry.chapterId = chapterId;
    entry.mangaId = mangaId;
    entry.page = progress.lastPageRead;
    entry.read = progress.isRead;
    entry.spot = progress.spot;
    entry.base = base;
    entry.mangaTitle = mangaTitle;
    if (const auto name = chapterNames.find(chapterId); name != chapterNames.end()) {
        entry.chapterName = name->second;
    }
    return entry;
}

void ProgressSaver::send(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync,
                         const core::ReadProgress& progress) {
    sync->sending(progress, self->clock());
    if (self->offline && self->queue != nullptr) {
        // Offline (WP-18b): into the queue, on the SD card within configSaveDelayMs. The base is what the Switch knew of
        // the server when the chapter opened; an entry already waiting keeps its older one (core::addWaitingSave).
        self->queue->add(self->entryFor(sync->chapterId(), progress, sync->base()));
        sync->saved(progress);
        if (self->onSaved) {
            self->onSaved(progress, true);
        }
        if (self->toModel) {
            self->toModel(progress);
        }
        self->tidy();
        return;
    }
    ++self->inFlight;
    self->client.setProgress(progress, self->never)
        .then([self, sync, progress](const api::ApiResult<core::ReadProgress>& result) {
            --self->inFlight;
            const bool ok = result.ok();
            if (ok) {
                sync->saved(progress);
                sync->serverAnswered(result.value());  // the base of a later save that fails (WP-15b)
                if (self->queue != nullptr && self->holding.count(sync->chapterId()) > 0) {
                    self->queue->removeHeld(sync->chapterId());  // its held copy, if a save failed before (WP-18b)
                }
            } else {
                sync->failed(self->clock());
                if (!self->closing && serverAway(result.error())) {
                    self->handOff(*sync);  // the held copy (WP-18b): the reader still retries it itself
                }
            }
            if (self->onSaved) {
                self->onSaved(progress, ok);
            }
            if (ok && self->toModel) {
                self->toModel(progress);
            }
            if (self->closing && ok) {
                // The reader moved on while this one was on its way.
                if (const auto more = sync->dueNow()) {
                    send(self, sync, *more);
                }
            }
            if (self->closing && !ok) {
                self->handOff(*sync);  // no more retries from here: the queue sends it once the server answers
            }
            self->tidy();
        });
}

void ProgressSaver::handOff(const core::ProgressSync& sync) {
    if (queue == nullptr) {
        return;
    }
    const std::optional<core::ProgressSync::NotSaved> notSaved = sync.notSaved();
    if (!notSaved) {
        return;
    }
    queue->add(entryFor(sync.chapterId(), notSaved->progress, notSaved->base));
}

void ProgressSaver::flush(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync,
                          bool now) {
    if (!sync) {
        return;
    }
    const auto progress = now ? sync->dueNow() : sync->due(self->clock());
    if (progress) {
        send(self, sync, *progress);
    }
}

void ProgressSaver::flushLeft(const std::shared_ptr<ProgressSaver>& self, bool now) {
    const auto chapters = self->left;  // send() may change the list
    for (const auto& sync : chapters) {
        flush(self, sync, now);
    }
    self->tidy();
}

void ProgressSaver::leave(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync) {
    if (!sync) {
        return;
    }
    self->left.push_back(sync);
    flush(self, sync, true);
    self->tidy();
}

void ProgressSaver::close(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync,
                          std::function<void()> done) {
    self->closing = true;
    self->whenDone.push_back(std::move(done));
    flush(self, sync, true);
    flushLeft(self, true);
    self->tidy();
}

void ProgressSaver::tidy() {
    left.erase(std::remove_if(left.begin(), left.end(),
                              [](const auto& sync) { return !sync->pending() && !sync->inFlight(); }),
               left.end());
    if (closing && inFlight == 0) {
        // Closed and everything handed over: the round may send this reader's chapters now (WP-18b).
        if (queue != nullptr) {
            for (int chapterId : holding) {
                queue->release(chapterId);
            }
        }
        holding.clear();
        std::vector<std::function<void()>> callbacks;
        callbacks.swap(whenDone);
        for (auto& callback : callbacks) {
            if (callback) {
                callback();
            }
        }
    }
}

}  // namespace snx::ui
