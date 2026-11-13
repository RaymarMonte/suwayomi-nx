#include "ui/reader/ProgressSaver.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

ProgressSaver::ProgressSaver(api::SuwayomiClient& c, std::function<std::int64_t()> time,
                             std::function<void(const core::ReadProgress&, bool)> saved)
    : client(c), clock(std::move(time)), onSaved(std::move(saved)) {}

void ProgressSaver::send(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync,
                         const core::ReadProgress& progress) {
    sync->sending(progress, self->clock());
    ++self->inFlight;
    self->client.setProgress(progress, self->never)
        .then([self, sync, progress](const api::ApiResult<core::ReadProgress>& result) {
            --self->inFlight;
            const bool ok = result.ok();
            if (ok) {
                sync->saved(progress);
                sync->serverAnswered(result.value());  // the base of a later save that fails (WP-15b)
            } else {
                sync->failed(self->clock());
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
    core::WaitingSave entry;
    entry.chapterId = sync.chapterId();
    entry.mangaId = mangaId;
    entry.page = notSaved->progress.lastPageRead;
    entry.read = notSaved->progress.isRead;
    entry.spot = notSaved->progress.spot;
    entry.base = notSaved->base;
    entry.mangaTitle = mangaTitle;
    if (const auto name = chapterNames.find(entry.chapterId); name != chapterNames.end()) {
        entry.chapterName = name->second;
    }
    queue->add(std::move(entry));
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
