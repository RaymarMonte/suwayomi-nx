#pragma once

#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"

namespace snx::core {

// One row of the Updates or History list (WP-15): a chapter and the manga it belongs to, from one `chapters` node with its
// `manga { ... }` part. The manga has only what those queries ask for (id, title, cover, inLibraryAt / inLibrary).
struct ChapterEntry {
    Chapter chapter;
    Manga manga;  // manga.id is the chapter's mangaId when the node had no readable manga part
};

}  // namespace snx::core
