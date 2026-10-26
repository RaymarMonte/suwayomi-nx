#pragma once

#include <cstdint>
#include <string>

namespace snx::core {

// One chapter. Fields a query didn't ask for keep the defaults below.
struct Chapter {
    int id = 0;
    int mangaId = 0;
    std::string name;            // "Chapter 3"
    double chapterNumber = -1;   // 12.5 is possible; -1 = unknown
    int sourceOrder = 0;         // position in the manga, 1 = oldest. Page URLs use it, not the id (SUWAYOMI-API.md)
    std::int64_t uploadDate = 0; // milliseconds since 1970 (UTC), 0 = unknown
    std::string scanlator;       // empty if none
    bool isRead = false;
    bool isBookmarked = false;
    bool isDownloaded = false;
    int lastPageRead = 0;        // page index, 0 = first page
    int pageCount = -1;          // -1 until the server has fetched the chapter's pages once
};

}  // namespace snx::core
