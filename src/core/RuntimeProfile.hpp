#pragma once

#include <cstdint>
#include <string_view>

#include "core/Json.hpp"
#include "core/RetryPolicy.hpp"
#include "core/RuntimeInfo.hpp"

// Every memory budget, prefetch count, concurrency and input limit the app uses (D12, D62, ARCHITECTURE.md "Launch modes and
// runtime profiles"). Modules read their limits from here, never from constants of their own.
// Full's numbers come from WP-00's measurements (application mode: a 3,184 MiB heap), tuned in WP-07 and WP-12. Lite's
// (applet mode, from the Album: a 259.7 MiB heap on the console) from WP-21's console measurements (D62).
namespace snx::core {

inline constexpr std::uint64_t kMiB = 1024 * 1024;
inline constexpr std::uint64_t kGiB = 1024 * kMiB;

struct RuntimeProfile {
    std::string_view name = "full";  // "full" (application mode) or "lite" (applet mode, from the Album; WP-21, D62)
    bool safeMode = false;           // the last run crashed (SAFETY.md S1): memory budgets halved, prefetch off

    // What it was made from.
    std::uint64_t totalMem = 0;
    std::uint64_t freeMem = 0;  // total - used at start-up (0 if used > total)

    // Memory budgets, in bytes.
    std::uint64_t imageCacheBytes = 0;     // decoded reader pages kept in RAM (LRU)
    std::uint64_t coverCacheBytes = 0;     // decoded cover thumbnails kept in RAM
    std::uint64_t textureBudgetBytes = 0;  // GPU textures alive at once (SAFETY.md S7)
    std::uint64_t maxDecodeBytes = 0;      // one decoded image, width x height x 4; checked from the header first (S6)
    float maxDecodeScale = 4.0f;           // decode up to this many times the screen size (or the original, if smaller);
                                           // 4 from WP-13c's console measurement (D48), safe mode and Lite 2
    float maxZoom = 4.0f;                  // the reader zooms a page up to this many times its fit (WP-13; safe mode: 2)

    // Reader prefetch.
    int prefetchBehind = 1;            // pages kept/fetched before the current one
    int prefetchAhead = 3;             // pages fetched after the current one
    int nextChapterPrefetchPages = 2;  // first pages of the next chapter, fetched near the end of a chapter
    bool backgroundPrefetch = true;    // fetch the next chapter in the background
    int webtoonTilesAround = 4;        // rows of tile textures kept above and below the screen in the webtoon strip (WP-13b)
    int stripScreensBehind = 1;        // WP-13b: the strip decodes the pages within this many screens above the screen
    int stripScreensAhead = 3;         // ... and within this many screens below it
    int stripZoomPages = 2;            // WP-13d: zoomed, at most this many pages on screen decode again sharper
    bool coverRowPrefetch = true;      // library grid: fetch the next row of covers

    // Reader (WP-11).
    int progressSyncMs = 5000;           // reading progress goes to the server at most this often (and when leaving a chapter)
    int progressRetryMaxMs = 60000;      // a failed save waits progressSyncMs, doubled per failure, at most this (WP-12)
    int readerMangaSettingsMax = 500;    // manga whose own direction and fit config.json remembers (S6: the file stays small)
    // The waiting-save queue (WP-15b): saves that failed as the reader closed, kept in progress-queue.json. WP-18b (D61):
    // also every save read offline, so room for long trips (500 chapters ~ 150 KiB).
    int pendingSavesMax = 500;                          // entries it keeps at most (the oldest goes)
    std::uint64_t progressQueueFileBytes = 256 * 1024;  // the file is read back at most this big (S6) and written to fit

    // Reading away from home (WP-18b, D61): ui::Connection's checks while offline, and what the Switch knows of its
    // downloaded chapters' progress (downloads/<server key>/progress.json).
    int offlineCheckMs = 30000;        // offline: the server is checked this often (the owner's 30 s), and at focus / waking
    int offlineNetworkPollMs = 2000;   // offline with no network: networkConnected() is asked this often (a check once true)
    int offlineSuspectGapMs = 5000;    // online: a request that couldn't reach the server checks it, at most this often
    int downloadedProgressBatch = 500;  // chapter ids per DownloadedProgress request (the refresh after a check)
    std::uint64_t switchProgressFileBytes = 512 * 1024;  // progress.json read back at most this big (S6), written to fit

    // Work in parallel, and how much to ask for at once.
    int httpConcurrency = 6;  // transfers at once (D5)
    int decodeThreads = 2;
    int libraryPageSize = 60;   // manga per LibraryByCategory request
    int chapterPageSize = 100;  // chapters per Chapters request
    int maxChapters = 10000;    // chapters of one manga kept in memory: the details screen stops loading there (S6, WP-10)

    // Updates and History (WP-15).
    int updatesPageSize = 50;            // chapters per LibraryUpdates request
    int historyPageSize = 50;            // chapters per ReadingHistory request
    int updatesFillPages = 5;            // pages in a row Updates loads to fill a screen before it waits for the user to scroll
    int maxUpdateRows = 2000;            // rows the Updates list holds at most (S6)
    int maxHistoryRows = 500;            // manga the History list holds at most (S6)
    int libraryUpdatePollMs = 1000;      // the library update's status is asked this often while it runs
    int libraryUpdateStartWaitMs = 10000;  // an update not seen starting by then (and nothing runs) is "nothing to update"

    // Downloads on the server (WP-17, D58).
    int serverDownloadPollMs = 1000;       // the server's queue is asked this often while it is watched and busy
    int serverDownloadStartWaitMs = 5000;  // a STOPPED queue this soon after our enqueue / resume shows "Starting..."
    int serverDownloadReloadMs = 3000;     // the details screen reloads its chapters at most this often while they finish
    int serverQueueMax = 1000;             // queue rows kept at most (S6); the tab says "and N more"
    int serverDownloadBatchMax = 1000;     // chapter ids per enqueue / delete request; more go in several
    int serverDownloadCheckMs = 500;       // an action without an answer this long: one read shows whether it went through

    // Downloads to the Switch's SD card (WP-18, D59). One chapter at a time; its pages this many at once, at Background
    // priority (the server makes at most 5 source calls at once: 2 leave room for the reader). Safe mode and applet: 1.
    int switchDownloadPagesAtOnce = 2;
    int switchDownloadPageTries = 3;      // a page that fails is tried this often in all before its chapter fails
    int switchDownloadRetryMs = 1000;     // ... this long after the first failure, doubled each time
    std::uint64_t switchDownloadLimitBytes = 8 * 1024ULL * 1024ULL * 1024ULL;  // Settings' "Space for downloads" default
    int switchQueueMax = 1000;            // chapters in the queue at most (S6)
    std::uint64_t switchQueueFileBytes = 512 * 1024;  // queue.json read back at most this big (S6), written to fit
    std::uint64_t switchMangaFileBytes = 1024 * 1024; // one manga.json read back at most this big (S6)
    int switchOpenWaitMs = 3000;          // a downloaded chapter opens without the server's progress after this long

    // Library tools (WP-24, D54).
    int searchMaxChars = 64;        // characters a library search keeps (the keyboard stops there too)
    int categoryNameMaxChars = 64;  // characters of a category name

    // Browse (WP-14, D56).
    int browseMaxItems = 1000;        // manga a source's grid holds at most (S6); paging stops there
    int sourcesMax = 500;             // sources the Browse tab lists at most (S6)
    int sourceFiltersMax = 300;       // filters of one source, a group's children counted (S6)
    int filterValuesMax = 500;        // choices of one Select / Sort filter (S6)
    int filterNameMaxBytes = 200;     // a filter's name and choices are cut there (S6)
    int filterTextMaxChars = 64;      // characters a Text filter keeps (the keyboard stops there too)
    int globalSearchConcurrency = 3;  // Search all asks this many sources at once; the rest wait
    int globalSearchCovers = 5;       // covers per source row in Search all
    int globalSearchSourcesMax = 100; // sources Search all asks at most

    // Network (net, WP-05). Timeouts are per request. Retries are for GETs only (core/RetryPolicy.hpp).
    int httpConnectTimeoutMs = 5000;                   // a LAN server answers a connect in milliseconds
    int httpTimeoutMs = 30000;                         // one whole JSON request
    // D60: a JSON request the server answers only after asking the source's website (GqlRequest::asksSource: browse a
    // source, a manga's details and chapters, a chapter's pages), until Settings -> "Wait for sources" picks another
    // (BrowseSettings::sourceWaitSeconds). Same as httpTimeoutMs: an out-of-the-box server answers in time; one that
    // passes Cloudflare checks through FlareSolverr can need a minute or more.
    int httpSourceTimeoutMs = 30000;
    int httpImageTimeoutMs = 60000;                    // one whole image download (the image module passes it)
    int httpMaxRedirects = 3;                          // followed redirects per request
    std::uint64_t httpMaxHeaderBytes = 64 * 1024;      // all response headers of one request together (S6)
    RetryPolicy httpRetry;                             // 2 retries, 250 ms then 500 ms apart
    std::uint64_t netThreadStackBytes = 512 * 1024;    // the network thread's stack (curl, TLS handshakes)

    // Untrusted input (SAFETY.md S6).
    JsonLimits json;                           // server answers and config.json
    std::uint64_t maxImageBytes = 32 * kMiB;   // one downloaded image
    int maxImageSide = 16384;                  // pixels, either side, read from the header before decoding

    // Images (image module, WP-07, D39).
    int maxTextureSide = 2048;                          // GPU texture side; bigger decoded images are cut into tiles (S7)
    int shrinkSlackPercent = 15;                        // a decoded image at most this much bigger (per side) than its box is
                                                        // kept as it is: the GPU scales it, the CPU shrink would cost ~100 ms
                                                        // on the Switch for a 4 % smaller page (WP-07 benchmark)
    int imageQueueLimit = 128;                          // image loads waiting or running at once; more fail as "busy" (S8)
    int imageAuthRetryMs = 1000;                        // an image answered HTTP 401 is tried once more after this (D38)
    std::uint64_t decodeThreadStackBytes = 1 * kMiB;    // each decode thread's stack (S8)
    std::uint64_t workerThreadStackBytes = 256 * 1024;  // other worker threads: the image module's disk thread (S8)

    // Memory-pressure guard (ARCHITECTURE.md): above pressurePercent of totalMem in use, caches shrink to pressureShrinkPercent
    // and prefetch pauses, until use is back under pressureRecoverPercent.
    int pressurePercent = 85;
    int pressureShrinkPercent = 50;
    int pressureRecoverPercent = 75;
    int pressureCheckMs = 2000;

    // SD card (SAFETY.md S5).
    std::uint64_t diskCacheBytes = 1 * kGiB;  // the page and cover cache on the SD card
    std::uint64_t minSdFreeBytes = 1 * kGiB;  // never write to the cache if the SD card would drop below this
    std::uint64_t logFileBytes = 1 * kMiB;    // logs/app.log moves to app.1.log before it grows past this (WP-08)
    int logFlushMs = 2000;                    // the log reaches the SD card at most this often, and at exit (WP-08)
    int configSaveDelayMs = 2000;             // config.json is saved once nothing changed for this long (S5, WP-08)

    // Crash ending (WP-21, D62): in applet mode a crash first tells the system "I'm leaving", then waits at most this long for
    // the system to end the process before ending it itself. Unused in application mode (the process ends at once, D35).
    int crashExitWaitMs = 5000;
    // Applet mode (WP-21, D62): with less free memory than this at start (a game open behind the Album takes some), the app
    // doesn't start: the guard's low-memory screen (SAFETY.md S12) says so. Enough for borealis plus the budget floors below.
    // Unused in application mode.
    std::uint64_t liteMinFreeBytes = 160 * kMiB;

    bool heavyUiEffects = true;  // extra fonts, animations
};

// Turns what platform::runtimeInfo() measured into budgets. Pure: same input, same profile.
// - Application mode: the "full" profile. Budgets are shares of the free memory with floors and caps:
//   image cache 40% (24 MiB .. 256 MiB, WP-12), covers 5% (8 .. 64 MiB), textures 15% (32 .. 256 MiB), one decode 10% (16 .. 128 MiB).
//   The floors are what showing one page needs; below them the memory-pressure guard has to cope.
// - Applet mode (from the Album, WP-21, D62): the "lite" profile. Image cache 25% (24 .. 64 MiB), covers 5% (8 .. 16 MiB),
//   textures 15% (32 .. 64 MiB), one decode 10% (16 .. 64 MiB); zoom and decode scale 2; the reader fetches one page ahead,
//   nothing of the next chapter; the strip keeps +-1 tile row and decodes 1 screen below; 2 transfers (borealis gives
//   applet mode 2 sockets), 1 decode thread, Search all one source at a time, downloads one page at a time; no extra UI
//   effects. Every other limit is Full's.
// - safeMode: memory budgets halved (not below the floors), prefetch off, webtoon tiles +-1. The same for both profiles.
// - Absurd input (used > total, total 0) gives the floors.
RuntimeProfile makeRuntimeProfile(LaunchMode mode, std::uint64_t totalMem, std::uint64_t usedMem, bool safeMode = false);

// Applet mode with less free memory at start than profile.liteMinFreeBytes: the app shows the guard's low-memory screen
// instead of starting (WP-21, D62, SAFETY.md S12). Always false in application mode.
bool tooLittleMemoryForLite(LaunchMode mode, const RuntimeProfile& profile);

}  // namespace snx::core
