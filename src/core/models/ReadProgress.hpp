#pragma once

namespace snx::core {

// Reading progress of one chapter: what the app sends with updateChapter, and what the server answers.
// The server caps lastPageRead to [0, pageCount] and saves 0 while pageCount is still -1 (pages never fetched), see SUWAYOMI-API.md.
struct ReadProgress {
    int chapterId = 0;
    int lastPageRead = 0;  // page index, 0 = first page
    bool isRead = false;   // sending: true marks the chapter read; false leaves the read flag as it is (re-reading never un-reads)
    int pageCount = -1;    // answer only
};

}  // namespace snx::core
