#pragma once

#include <optional>

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
};

}  // namespace snx::core
