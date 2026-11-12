#include "core/ProgressSync.hpp"

#include <algorithm>
#include <cmath>

namespace snx::core {

namespace {

// A spot as the note stores it: in ten-thousandths (4 decimals), so a move smaller than that is no change.
long spotKey(const std::optional<double>& spot) {
    return spot ? std::lround(std::clamp(*spot, 0.0, 1.0) * 10000.0) : -1;
}

}  // namespace

ProgressSync::ProgressSync(int chapterId, int serverPage, bool serverRead, int intervalMs, int retryMaxMs, std::int64_t nowMs,
                           std::optional<double> serverSpot)
    : chapterId_(chapterId),
      intervalMs_(intervalMs),
      retryMaxMs_(retryMaxMs),
      savedPage_(serverPage),
      savedRead_(serverRead),
      wantPage_(serverPage),
      savedSpot_(serverSpot),
      lastSendMs_(nowMs) {}

void ProgressSync::shown(int page, bool last) {
    wantPage_ = page < 0 ? 0 : page;
    if (last && !savedRead_) {
        wantRead_ = true;
    }
}

void ProgressSync::shownAt(int page, double spot, bool read) {
    shown(page, read);
    wantSpot_ = std::clamp(spot, 0.0, 1.0);
}

bool ProgressSync::pending() const {
    return unsaved().has_value();
}

std::optional<ReadProgress> ProgressSync::unsaved() const {
    const bool newlyRead = wantRead_ && !savedRead_;
    const bool spotMoved = wantSpot_ && (wantPage_ != savedPage_ || spotKey(wantSpot_) != spotKey(savedSpot_));
    if (wantPage_ == savedPage_ && !newlyRead && !spotMoved) {
        return std::nullopt;
    }
    ReadProgress progress;
    progress.chapterId = chapterId_;
    progress.lastPageRead = wantPage_;
    progress.isRead = newlyRead;
    progress.spot = wantSpot_;
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
    if (progress.spot) {
        savedSpot_ = progress.spot;
    }
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
