#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/AppConfig.hpp"
#include "core/Json.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"

// Downloads on the Switch's own SD card (WP-18, D59): where the files go, the three small files that describe them (read
// back tolerantly, S6), the space rule, what the Switch downloads tab shows, and how sizes are written. The app's side (the
// files on the SD card, the queue that downloads) is ui/downloads/DownloadStore and ui/downloads/SwitchDownloads. Pure:
// unit-tested (tests/unit/test_core_switch_downloads.cpp) and fuzzed (tests/fuzz/fuzz_switch_downloads.cpp).
//
//   downloads/<server key>/server.json             {"urls": [...]}: the addresses of this server
//   downloads/<server key>/queue.json (+ .bak)     the queue: paused flag + entries
//   downloads/<server key>/m<mangaId>/manga.json (+ .bak)   the manga's fields + its finished chapters (the index)
//   downloads/<server key>/m<mangaId>/cover.img    the cover's bytes as downloaded
//   downloads/<server key>/m<mangaId>/c<chapterId>/0000.jpg ...   the pages: index + the format sniffed from the bytes
namespace snx::core {

inline constexpr const char* kSwitchDownloadsDir = "downloads";
inline constexpr const char* kSwitchServerFile = "server.json";
inline constexpr const char* kSwitchQueueFile = "queue.json";
inline constexpr const char* kSwitchMangaFile = "manga.json";
inline constexpr const char* kSwitchCoverFile = "cover.img";
inline constexpr const char* kSwitchBackupSuffix = ".bak";  // SafeFs::writeAtomic's Backup::Keep name

// --- Names ---

// A server address as the downloads file it: config.json's form with a trailing "/" removed.
std::string downloadServerUrl(std::string_view url);
// 16 lower-case hex digits of FNV-1a (64 bit) of downloadServerUrl(url): the folder of the server the address was first
// saved for. Another address of the same server joins that folder's server.json (the "Same server?" question).
std::string switchServerKey(std::string_view url);
std::string switchServerDir(const std::string& key);                               // "downloads/<key>"
std::string switchMangaDir(const std::string& key, int mangaId);                   // "downloads/<key>/m12"
std::string switchChapterDir(const std::string& key, int mangaId, int chapterId);  // "downloads/<key>/m12/c578"
// "m12" -> 12 (prefix 'm'), "c578" -> 578 (prefix 'c'). Nothing for anything else ("m", "m-1", "m12x", "manga.json").
std::optional<int> idOfFolder(std::string_view name, char prefix);

// A page's format, one letter each in a chapter entry: what its bytes turned out to be.
inline constexpr char kPageJpeg = 'j';
inline constexpr char kPagePng = 'p';
inline constexpr char kPageWebp = 'w';
inline constexpr char kPageGif = 'g';
inline constexpr char kPageUnusable = '-';  // the server listed the page in a form the app can't use: no file
bool isPageFormat(char letter);             // one of the five above
// "0000.jpg", "0012.webp" (index 0..9999 with 4 digits, more digits above). "" for kPageUnusable or an unknown letter.
std::string switchPageName(int index, char format);
// "0012.webp" -> {12, 'w'}; nothing for any other name (a ".tmp" left by a power cut, "cover.img").
struct PageName {
    int index = 0;
    char format = kPageJpeg;
};
std::optional<PageName> parsePageName(std::string_view name);

// --- What is on the SD card ---

inline constexpr std::size_t kSwitchTextMaxBytes = 512;           // a title, name, author, genre, URL (cut there, S6)
inline constexpr std::size_t kSwitchDescriptionMaxBytes = 8192;   // a manga's description
inline constexpr std::size_t kSwitchGenresMax = 64;
inline constexpr int kSwitchPagesMax = 10000;                     // pages of one chapter (a longer page list is refused)
inline constexpr std::size_t kSwitchUrlsMax = 16;                 // addresses in one server.json

// One finished chapter (manga.json). Its fields are the server's when it was downloaded.
struct SwitchChapter {
    int id = 0;
    std::string name;
    double chapterNumber = -1;
    int sourceOrder = 0;
    std::string scanlator;
    std::int64_t uploadDate = 0;    // milliseconds since 1970, 0 = unknown
    std::string formats;            // one letter per page (kPageJpeg ...): the page count is its length
    std::uint64_t bytes = 0;        // the page files' bytes together
    std::int64_t downloadedAt = 0;  // seconds since 1970 by the console's clock
    std::vector<int> damaged;       // pages the reader found missing, cut short or undecodable (sorted, no repeats)

    int pages() const { return static_cast<int>(formats.size()); }
};
SwitchChapter switchChapterFrom(const Chapter& chapter);  // the fields above that a Chapter has (texts cut)
// Back to a Chapter (WP-18b's offline list; the details screen's filter): id, manga, name, number, order, scanlator, date,
// the page count, isDownloaded false (that is the server's flag).
Chapter chapterFrom(const SwitchChapter& chapter, int mangaId);

// One manga folder's index (manga.json): what the details screen shows, and its finished chapters by sourceOrder.
struct SwitchManga {
    int id = 0;
    std::string title;
    std::string author;
    std::string artist;
    std::string description;
    std::vector<std::string> genres;
    std::string status;  // the server's MangaStatus name ("ONGOING"), "" = unknown
    std::string sourceId;
    std::string sourceName;
    std::string thumbnailUrl;     // relative to the server, as the server gave it
    std::uint64_t coverBytes = 0;  // cover.img's size; 0 = no cover on the SD card
    std::vector<SwitchChapter> chapters;

    std::uint64_t bytes() const;            // the chapters' pages + the cover
    const SwitchChapter* chapter(int chapterId) const;
};
SwitchManga switchMangaFrom(const Manga& manga);  // the manga's fields (cut), no chapters, no cover
// Adds or replaces the chapter (by id) and keeps the chapters by sourceOrder.
void putChapter(SwitchManga& manga, SwitchChapter chapter);
bool removeChapter(SwitchManga& manga, int chapterId);  // false if it wasn't there

inline constexpr int kSwitchFileVersion = 1;

// manga.json: {"version": 1, "manga": {...}, "chapters": [ {...}, ... ]}, one chapter per line.
std::string writeMangaFile(const SwitchManga& manga);
struct MangaFileLoad {
    bool usable = false;  // an object with a manga id; else try the .bak
    SwitchManga manga;
    int skipped = 0;                    // broken chapter entries left out
    std::vector<std::string> problems;  // for the log
};
// Tolerant (S6): a chapter without an id, with a page list over kSwitchPagesMax or with a format letter it doesn't know is
// skipped and counted; the same chapter twice keeps the later one; texts are cut; unknown fields are ignored. Never throws.
MangaFileLoad readMangaFile(std::string_view text, const JsonLimits& limits);

// server.json: {"version": 1, "urls": ["http://...", ...]}.
std::string writeServerFile(const std::vector<std::string>& urls);
// The addresses (downloadServerUrl form, no repeats, at most kSwitchUrlsMax; longer than kSwitchTextMaxBytes dropped).
// Nothing when the text isn't a server file.
std::optional<std::vector<std::string>> readServerFile(std::string_view text, const JsonLimits& limits);
// WP-18c ("Same server?" -> Same server): the addresses with `url` added at the end (downloadServerUrl form; already there:
// unchanged). At most kSwitchUrlsMax: when full, the oldest address that isn't `keep` (the saved one) goes first.
std::vector<std::string> addServerUrl(std::vector<std::string> urls, std::string_view url, std::string_view keep);

// One chapter in the queue (queue.json). It carries everything the chapter's manga.json entry needs once it is done.
struct SwitchQueueEntry {
    int mangaId = 0;
    std::string mangaTitle;
    std::string thumbnailUrl;
    SwitchChapter chapter;  // the chapter's fields; formats and bytes stay empty until it is done
    bool failed = false;    // a page failed every try: it waits for Retry
    int failedPage = -1;    // the first page that failed (0-based), -1 = none / the page list failed
    int pageCount = 0;      // the page list's length when it was last asked for, 0 = not yet
    std::string error;      // why it failed, one line (for the row)
};
struct SwitchQueue {
    bool paused = false;  // paused by the user (Pause); "waiting" for space or the server is never saved
    std::vector<SwitchQueueEntry> entries;  // the download order
};
const SwitchQueueEntry* findEntry(const SwitchQueue& queue, int chapterId);
// queue.json: {"version": 1, "paused": false, "entries": [ {...}, ... ]}, one entry per line.
std::string writeQueueFile(const SwitchQueue& queue);
struct QueueFileLoad {
    bool usable = false;  // an object with an "entries" list; else try the .bak
    SwitchQueue queue;
    int skipped = 0;
    std::vector<std::string> problems;
};
// Tolerant: an entry without a manga or chapter id is skipped and counted; a chapter twice keeps the first; at most
// `maxEntries` (RuntimeProfile::switchQueueMax) are kept, the first ones.
QueueFileLoad readQueueFile(std::string_view text, const JsonLimits& limits, std::size_t maxEntries);

// --- Space ---

// Whether `adding` more bytes may be written (before every page). limit 0 = no limit of our own. freeBytes: what the SD card
// says (nothing = it can't say). minFree: RuntimeProfile::minSdFreeBytes, always left free.
enum class SpaceCheck { Fits, LimitFull, SdFull, Unknown };
SpaceCheck checkSpace(std::uint64_t used, std::uint64_t adding, std::uint64_t limit, std::optional<std::uint64_t> freeBytes,
                      std::uint64_t minFree);

// Settings' "Space for downloads": the limit in bytes (0 = no limit). A missing or unknown choice is the profile's default.
std::uint64_t switchLimitBytes(const SwitchDownloadSettings& settings, std::uint64_t profileDefault);

// --- What the tab shows ---

// Why a queue with chapters to download doesn't download now.
enum class SwitchWait {
    None,
    LimitFull,   // "Paused: the space for downloads is full (8 GB)"
    SdFull,      // "Paused: the SD card is nearly full (1 GB left)"
    SpaceUnknown,// the SD card can't say how much is free: nothing is written
    Server,      // "Waiting for the server"
    Login,       // "Can't log in to the server"
    Memory,      // the image loader's memory guard paused prefetch
    Background,  // the app isn't on screen (HOME, sleep)
};
const char* switchWaitName(SwitchWait wait);  // "none", "limit_full", "sd_full", "space_unknown", "server", "login", ...

enum class SwitchQueueView {
    Idle,     // no entries
    Running,  // downloading
    Paused,   // paused by the user, chapters waiting
    Waiting,  // chapters waiting, held back by a SwitchWait reason
    Stopped,  // only failed entries ("Stopped: 2 failed")
};
// entries: in the queue; failed: of them. paused: by the user.
SwitchQueueView switchQueueView(int entries, int failed, bool paused, SwitchWait wait);
const char* switchQueueViewName(SwitchQueueView view);  // "idle", "running", "paused", "waiting", "stopped"

// "0 B", "512 B", "12 KB", "1.8 MB", "340 MB", "2.3 GB", "8 GB" (1 KB = 1024 bytes; one decimal below 10, ".0" dropped).
std::string bytesText(std::uint64_t bytes);

// The bulk choices ("Download to Switch...") use core::chaptersToDownload (ServerDownloads.hpp) with the chapters on this
// Switch or queued here as its skip set.

}  // namespace snx::core
