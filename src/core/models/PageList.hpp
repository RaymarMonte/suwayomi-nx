#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/models/Chapter.hpp"

namespace snx::core {

// KOReader sync knows a newer position in this chapter (fetchChapterPages.syncConflict).
struct SyncConflict {
    std::string deviceName;
    int remotePage = 0;
};

// fetchChapterPages: what the reader needs to open a chapter.
struct PageList {
    // One URL per page, relative to the server ("/api/v1/manga/1/chapter/1/page/0"). The index is the page number the server
    // uses for lastPageRead, so entries are never dropped: a page the server listed in an odd form stays as an empty string,
    // and the reader shows it as broken.
    std::vector<std::string> pages;
    Chapter chapter;  // the fields the query asked for (id, pageCount, lastPageRead, ...)
    std::optional<SyncConflict> syncConflict;
};

}  // namespace snx::core
