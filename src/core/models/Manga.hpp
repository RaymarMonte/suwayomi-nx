#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/models/Category.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Source.hpp"

namespace snx::core {

// The server's MangaStatus enum. Names the app doesn't know become Unknown.
enum class MangaStatus { Unknown, Ongoing, Completed, Licensed, PublishingFinished, Cancelled, OnHiatus };

MangaStatus mangaStatusFromName(std::string_view name);  // "ONGOING" -> Ongoing
std::string_view mangaStatusName(MangaStatus status);     // Ongoing -> "ONGOING"

// One manga. Fields a query didn't ask for keep the defaults below.
struct Manga {
    int id = 0;
    std::string title;
    std::string thumbnailUrl;  // relative to the server ("/api/v1/manga/1/thumbnail"), empty if none
    std::string author;        // empty if unknown
    std::string artist;
    std::string description;
    std::vector<std::string> genre;
    MangaStatus status = MangaStatus::Unknown;
    bool inLibrary = false;
    int unreadCount = 0;
    int downloadCount = 0;
    std::optional<Source> source;
    std::vector<Category> categories;  // the library query asks only for ids (names stay empty)
    std::optional<Chapter> firstUnreadChapter;  // where "Continue reading" starts; none if everything is read
    // When the chapter read most recently was read (lastReadChapter.lastReadAt, the server's time stamp: bigger = later).
    // 0 = never read, or the query didn't ask. The library sorts by it (LibrarySort::LastRead).
    std::int64_t lastReadAt = 0;
};

}  // namespace snx::core
