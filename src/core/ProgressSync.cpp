#include "core/ProgressSync.hpp"

namespace snx::core {

ProgressSync::ProgressSync(int chapterId, int serverPage, bool serverRead, int intervalMs, std::int64_t nowMs)
    : chapterId_(chapterId),
      intervalMs_(intervalMs),
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
    savedPage_ = progress.lastPageRead;
    if (progress.isRead) {
        savedRead_ = true;
        wantRead_ = false;
    }
}

void ProgressSync::failed() {
    inFlight_ = false;
}

}  // namespace snx::core
