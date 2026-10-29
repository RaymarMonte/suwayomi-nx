#pragma once

#include <cstdint>
#include <string_view>

#include "core/Json.hpp"
#include "core/RetryPolicy.hpp"
#include "core/RuntimeInfo.hpp"

// Every memory budget, prefetch count, concurrency and input limit the app uses (D12 amended by D21, ARCHITECTURE.md
// "Launch modes and runtime profiles"). Modules read their limits from here, never from constants of their own.
// The numbers are first guesses from WP-00's measurements (application mode: a 3,184 MiB heap); WP-07 and WP-12 tune them.
namespace snx::core {

inline constexpr std::uint64_t kMiB = 1024 * 1024;
inline constexpr std::uint64_t kGiB = 1024 * kMiB;

struct RuntimeProfile {
    std::string_view name = "full";  // "full" (application mode, the only one used, D21) or "applet" (see makeRuntimeProfile)
    bool safeMode = false;           // the last run crashed (SAFETY.md S1): memory budgets halved, prefetch off

    // What it was made from.
    std::uint64_t totalMem = 0;
    std::uint64_t freeMem = 0;  // total - used at start-up (0 if used > total)

    // Memory budgets, in bytes.
    std::uint64_t imageCacheBytes = 0;     // decoded reader pages kept in RAM (LRU)
    std::uint64_t coverCacheBytes = 0;     // decoded cover thumbnails kept in RAM
    std::uint64_t textureBudgetBytes = 0;  // GPU textures alive at once (SAFETY.md S7)
    std::uint64_t maxDecodeBytes = 0;      // one decoded image, width x height x 4; checked from the header first (S6)
    float maxDecodeScale = 2.0f;           // decode up to this many times the screen size (or the original, if smaller)

    // Reader prefetch.
    int prefetchBehind = 1;            // pages kept/fetched before the current one
    int prefetchAhead = 3;             // pages fetched after the current one
    int nextChapterPrefetchPages = 2;  // first pages of the next chapter, fetched near the end of a chapter
    bool backgroundPrefetch = true;    // fetch the next chapter in the background
    int webtoonTilesAround = 4;        // decoded tiles kept above and below the screen in the vertical reader
    bool coverRowPrefetch = true;      // library grid: fetch the next row of covers

    // Reader (WP-11).
    int progressSyncMs = 5000;           // reading progress goes to the server at most this often (and when leaving a chapter)
    int progressRetryMaxMs = 60000;      // a failed save waits progressSyncMs, doubled per failure, at most this (WP-12)
    int readerMangaSettingsMax = 500;    // manga whose own direction and fit config.json remembers (S6: the file stays small)

    // Work in parallel, and how much to ask for at once.
    int httpConcurrency = 6;  // transfers at once (D5)
    int decodeThreads = 2;
    int libraryPageSize = 60;   // manga per LibraryByCategory request
    int chapterPageSize = 100;  // chapters per Chapters request
    int maxChapters = 10000;    // chapters of one manga kept in memory: the details screen stops loading there (S6, WP-10)

    // Network (net, WP-05). Timeouts are per request. Retries are for GETs only (core/RetryPolicy.hpp).
    int httpConnectTimeoutMs = 5000;                   // a LAN server answers a connect in milliseconds
    int httpTimeoutMs = 30000;                         // one whole JSON request
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

    bool heavyUiEffects = true;  // extra fonts, animations
};

// Turns what platform::runtimeInfo() measured into budgets. Pure: same input, same profile.
// - Application mode: the "full" profile. Budgets are shares of the free memory with floors and caps:
//   image cache 40% (24 MiB .. 256 MiB, WP-12), covers 5% (8 .. 64 MiB), textures 15% (32 .. 256 MiB), one decode 10% (16 .. 128 MiB).
//   The floors are what showing one page needs; below them the memory-pressure guard has to cope.
// - safeMode: memory budgets halved (not below the floors), prefetch off, webtoon tiles +-1.
// - Applet mode: the app refuses to run there (D21) and exits before using a profile, but this still returns something small
//   and safe ("applet": 2 transfers like the 2 sockets, 1 decode thread, +-1 page, caps of 64 MiB). WP-21 turns it into Lite.
// - Absurd input (used > total, total 0) gives the floors.
RuntimeProfile makeRuntimeProfile(LaunchMode mode, std::uint64_t totalMem, std::uint64_t usedMem, bool safeMode = false);

}  // namespace snx::core
