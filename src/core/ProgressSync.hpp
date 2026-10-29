#pragma once

#include <cstdint>
#include <optional>

#include "core/models/ReadProgress.hpp"

// When the reader saves reading progress to the server (WP-11): the page on screen as lastPageRead, at most once every
// intervalMs (RuntimeProfile::progressSyncMs), counted from when the chapter opened; isRead as soon as the chapter's last page
// was shown (that one doesn't wait); and everything left at once when the reader leaves the chapter (dueNow). One update at a
// time; a failed one is tried again at the next chance. isRead is only ever sent as true: re-reading never un-reads.
// Pure and not thread-safe: the reader calls it on the main thread with the app's clock. One per opened chapter.
namespace snx::core {

class ProgressSync {
  public:
    // What the server has for the chapter (fetchChapterPages' answer), and when it opened.
    ProgressSync(int chapterId, int serverPage, bool serverRead, int intervalMs, std::int64_t nowMs);

    // A page is on screen. `last`: it is the chapter's last page, so the chapter is read from now on.
    void shown(int page, bool last);

    // The update to send now, if one is due (see above). nowMs: the same clock as the constructor's.
    std::optional<ReadProgress> due(std::int64_t nowMs) const;
    // The update to send now without waiting for intervalMs (leaving the chapter), if anything isn't saved and nothing is on
    // its way.
    std::optional<ReadProgress> dueNow() const;

    // The update from due()/dueNow() went out.
    void sending(const ReadProgress& progress, std::int64_t nowMs);
    // ... and the server saved it.
    void saved(const ReadProgress& progress);
    // ... and it failed: tried again when due() says so (intervalMs after it went out).
    void failed();

    int chapterId() const { return chapterId_; }
    bool pending() const;  // something on screen isn't saved yet (it may be on its way)
    bool inFlight() const { return inFlight_; }
    bool read() const { return wantRead_ || savedRead_; }  // the chapter is read (on the server, or as soon as saved)

  private:
    std::optional<ReadProgress> unsaved() const;

    int chapterId_;
    int intervalMs_;
    int savedPage_;
    bool savedRead_;
    int wantPage_;
    bool wantRead_ = false;
    bool inFlight_ = false;
    std::int64_t lastSendMs_;
};

}  // namespace snx::core
