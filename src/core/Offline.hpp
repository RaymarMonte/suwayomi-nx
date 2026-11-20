#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Json.hpp"
#include "core/ProgressQueue.hpp"
#include "core/SwitchDownloads.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "core/models/ReadProgress.hpp"

// Reading away from home (WP-18b, D61): the rules the offline screens and the progress sync use. The server's clock (the
// Date header), what the Switch knows of its downloaded chapters' progress (downloads/<server key>/progress.json, read back
// tolerantly, S6), and how a downloaded chapter, what the server last said about it and a waiting save of it make the
// chapter the screens show. The app's side: ui/main/Connection (online / offline), ui/downloads/SwitchDownloads (the file,
// the refresh) and the offline Library / details / reader. Pure: unit-tested (tests/unit/test_core_offline.cpp) and fuzzed
// (tests/fuzz/fuzz_switch_downloads.cpp).
namespace snx::core {

// --- The server's clock ---

inline constexpr std::size_t kHttpDateMaxBytes = 64;
// "Fri, 20 Nov 2026 05:03:18 GMT" (RFC 1123, the form every HTTP/1.1 server sends in its Date header) -> seconds since
// 1970 (UTC). Nothing for anything else: another date form, a field out of range, more than kHttpDateMaxBytes (S6: the
// header comes from the network).
std::optional<std::int64_t> parseHttpDate(std::string_view text);

// --- What the Switch knows of its downloaded chapters (downloads/<server key>/progress.json) ---

inline constexpr const char* kSwitchProgressFile = "progress.json";
inline constexpr std::size_t kSpotNoteMaxBytes = 64;  // a strip spot note is cut there (it is ~10 bytes)

// A chapter the server no longer has (answer 8): its progress lives on the Switch only.
struct LocalProgress {
    int page = 0;
    bool read = false;
    std::optional<double> spot;  // the strip's spot on `page`
    std::int64_t at = 0;         // when it was read, seconds since 1970 by the console's clock
    bool operator==(const LocalProgress& other) const {
        return page == other.page && read == other.read && spot == other.spot && at == other.at;
    }
};

// One downloaded chapter's progress as the Switch knows it.
struct KnownProgress {
    ServerProgress server;  // the server's page, read flag, lastReadAt, bookmark and spot note as last seen (no downloaded)
    bool gone = false;      // the server no longer lists the chapter
    std::optional<LocalProgress> local;  // gone chapters only
    bool operator==(const KnownProgress& other) const;
    bool operator!=(const KnownProgress& other) const { return !(*this == other); }
};
using SwitchProgress = std::map<int, KnownProgress>;  // by chapter id

// progress.json, version 1: {"version": 1, "chapters": [ {...}, ... ]}, one chapter per line, by id.
std::string writeProgressFile(const SwitchProgress& progress);
struct ProgressFileLoad {
    bool usable = false;  // an object with a "chapters" list; else try the .bak
    SwitchProgress progress;
    int skipped = 0;                    // broken entries left out
    std::vector<std::string> problems;  // for the log
};
// Tolerant (S6): an entry without a chapter id or with a page below 0 is skipped and counted; texts are cut; a spot outside
// 0..1 is left out; the same chapter twice keeps the later one; unknown fields are ignored. Never throws.
ProgressFileLoad readProgressFile(std::string_view text, const JsonLimits& limits);
// Leaves out the entries with the highest ids until the file is at most `maxBytes` (RuntimeProfile::switchProgressFileBytes:
// what is read back, S6). Returns how many went (those chapters show what manga.json says until the next refresh).
int fitProgressFile(SwitchProgress& progress, std::size_t maxBytes);

// --- What the offline screens show ---

// A downloaded chapter as the offline list and the reader show it: its fields from manga.json (chapterFrom); then what
// the Switch knows (`known`: the server's page, read flag, stamp, bookmark and spot note as last seen; a gone chapter
// instead its local progress, and goneFromServer); then a waiting save of it on top (its page and spot, read stays read,
// its time on the server's clock: madeAt + clockOffsetSeconds, if later). Either may be nullptr.
Chapter offlineChapter(const SwitchChapter& downloaded, int mangaId, const KnownProgress* known, const WaitingSave* waiting,
                       std::int64_t clockOffsetSeconds);
// The manga's fields from its manga.json (in the library: the offline shelf only shows downloaded manga).
Manga mangaFrom(const SwitchManga& manga);
// The offline Library's item: mangaFrom() with the badge (downloaded chapters not read) and the stamp (the newest
// lastReadAt) of its merged chapters.
Manga offlineManga(const SwitchManga& manga, const std::vector<Chapter>& chapters);
// Chapters left out between two in reading order: 0 when `to` comes right after `from` (by sourceOrder), 2 for chapter 3
// after chapter 0... (the reader's "The 2 chapters before it aren't on this Switch.").
int chaptersSkipped(const Chapter& from, const Chapter& to);
// The offline Library's search: `search` (trimmed by the caller) inside `title`, ASCII letters compared without case,
// other bytes as they are. An empty search matches everything.
bool titleMatches(std::string_view title, std::string_view search);

}  // namespace snx::core
