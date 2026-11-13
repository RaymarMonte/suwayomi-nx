#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace snx::core {

// Reading progress of one chapter: what the app sends with updateChapter, and what the server answers.
// The server caps lastPageRead to [0, pageCount] and saves 0 while pageCount is still -1 (pages never fetched), see SUWAYOMI-API.md.
struct ReadProgress {
    int chapterId = 0;
    int lastPageRead = 0;  // page index, 0 = first page
    bool isRead = false;   // sending: true marks the chapter read; false leaves the read flag as it is (re-reading never un-reads)
    int pageCount = -1;    // answer only
    // WP-13b, the webtoon strip: how far down page lastPageRead the strip's middle line is (0 = its top, 1 = its bottom).
    // Sent with the page as the chapter's meta note "snx_strip_spot" (core::stripSpotValue); none from the page reader.
    std::optional<double> spot;
    // WP-15b: when the server last stamped the chapter (seconds since 1970; it stamps every save that sends lastPageRead).
    // Answer only; the waiting-save queue keeps it as the base of a save that failed later.
    std::int64_t lastReadAt = 0;
};

// What the server has for a chapter's reading progress (WP-15b): the waiting-save queue compares it with a waiting save
// (core/ProgressQueue.hpp). From fetchChapterPages, a save's answer or ChapterState.
struct ServerProgress {
    int page = 0;                 // lastPageRead
    bool read = false;            // isRead
    std::int64_t lastReadAt = 0;  // seconds; 0 = never stamped
    std::string spotNote;         // the strip's meta note "snx_strip_spot" ("" if none); not part of a base

    // The same page, read flag and stamp (the note isn't compared).
    bool sameAs(const ServerProgress& other) const {
        return page == other.page && read == other.read && lastReadAt == other.lastReadAt;
    }
};

}  // namespace snx::core
