#include "core/ProgressSync.hpp"

#include <algorithm>

namespace snx::core {

ProgressSync::ProgressSync(int chapterId, int serverPage, bool serverRead, int intervalMs, int retryMaxMs, std::int64_t nowMs)
    : chapterId_(chapterId),
      intervalMs_(intervalMs),
      retryMaxMs_(retryMaxMs),
      savedPage_(serverPage),
      savedRead_(serverRead),
      wantPage_(serverPage),
      lastSendMs_(nowMs) {}

void ProgressSync::shown(int page, bool last) {
    wantPage_ = page < 0 ? 0 : page;
    if (last && !savedRead_) {
        wantRead_ = true;
    }
}

bool ProgressSync::pending() const {
    return unsaved().has_value();
}

std::optional<ReadProgress> ProgressSync::unsaved() const {
    const bool newlyRead = wantRead_ && !savedRead_;
    if (wantPage_ == savedPage_ && !newlyRead) {
        return std::nullopt;
    }
    ReadProgress progress;
    progress.chapterId = chapterId_;
    progress.lastPageRead = wantPage_;
    progress.isRead = newlyRead;
    return progress;
}

std::optional<ReadProgress> ProgressSync::dueNow() const {
    if (inFlight_) {
        return std::nullopt;
    }
    return unsaved();
}

std::optional<ReadProgress> ProgressSync::due(std::int64_t nowMs) const {
    if (failures_ > 0 && nowMs < retryAtMs_) {
        return std::nullopt;
    }
    std::optional<ReadProgress> progress = dueNow();
    if (!progress || progress->isRead || nowMs - lastSendMs_ >= intervalMs_) {
        return progress;
    }
    return std::nullopt;
}

void ProgressSync::sending(const ReadProgress&, std::int64_t nowMs) {
    inFlight_ = true;
    lastSendMs_ = nowMs;
}

void ProgressSync::saved(const ReadProgress& progress) {
    inFlight_ = false;
    failures_ = 0;
    savedPage_ = progress.lastPageRead;
    if (progress.isRead) {
        savedRead_ = true;
        wantRead_ = false;
    }
}

void ProgressSync::failed(std::int64_t nowMs) {
    inFlight_ = false;
    ++failures_;
    // intervalMs, doubled for each failure after the first, at most retryMaxMs.
    std::int64_t waitMs = intervalMs_;
    for (int i = 1; i < failures_ && waitMs < retryMaxMs_; ++i) {
        waitMs *= 2;
    }
    retryAtMs_ = nowMs + std::min<std::int64_t>(waitMs, retryMaxMs_);
}

}  // namespace snx::core
