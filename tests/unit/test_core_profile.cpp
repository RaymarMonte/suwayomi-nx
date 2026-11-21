#include <cstdint>
#include <limits>

#include <doctest/doctest.h>

#include "core/RuntimeProfile.hpp"

using namespace snx::core;

namespace {

// WP-00's measurement on the owner's v1 console, application mode: a 3,184 MiB heap. A few dozen MiB are in use at start-up.
constexpr std::uint64_t kSwitchHeap = 3184 * kMiB;
constexpr std::uint64_t kStartUsed = 40 * kMiB;

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("RuntimeProfile: memory budgets by free memory (table)") {
        struct Case {
            const char* what;
            std::uint64_t total, used;
            bool safe;
            std::uint64_t image, cover, texture, decode;
        };
        const std::uint64_t free512 = 512 * kMiB - kStartUsed;  // desktop --mem-limit=512
        const Case cases[] = {
            // The console: every budget at its cap.
            {"switch, application mode", kSwitchHeap, kStartUsed, false, 256 * kMiB, 64 * kMiB, 256 * kMiB, 128 * kMiB},
            {"switch, safe mode", kSwitchHeap, kStartUsed, true, 128 * kMiB, 32 * kMiB, 128 * kMiB, 64 * kMiB},
            // Desktop with --mem-limit=512: shares of the free memory.
            {"desktop --mem-limit=512", 512 * kMiB, kStartUsed, false, free512 / 100 * 40, free512 / 100 * 5,
             free512 / 100 * 15, free512 / 100 * 10},
            // A tight limit: the floors.
            {"desktop --mem-limit=128", 128 * kMiB, 100 * kMiB, false, 24 * kMiB, 8 * kMiB, 32 * kMiB, 16 * kMiB},
            {"floors stay in safe mode", 128 * kMiB, 100 * kMiB, true, 24 * kMiB, 8 * kMiB, 32 * kMiB, 16 * kMiB},
            // Absurd input.
            {"used > total", 100 * kMiB, 200 * kMiB, false, 24 * kMiB, 8 * kMiB, 32 * kMiB, 16 * kMiB},
            {"nothing known", 0, 0, false, 24 * kMiB, 8 * kMiB, 32 * kMiB, 16 * kMiB},
            {"huge", std::numeric_limits<std::uint64_t>::max(), 0, false, 256 * kMiB, 64 * kMiB, 256 * kMiB, 128 * kMiB},
            {"a PC with 32 GiB and no limit", 32 * kGiB, 200 * kMiB, false, 256 * kMiB, 64 * kMiB, 256 * kMiB, 128 * kMiB},
        };
        for (const auto& c : cases) {
            CAPTURE(c.what);
            const RuntimeProfile p = makeRuntimeProfile(LaunchMode::Application, c.total, c.used, c.safe);
            CHECK(p.name == "full");
            CHECK(p.safeMode == c.safe);
            CHECK(p.totalMem == c.total);
            CHECK(p.freeMem == (c.total > c.used ? c.total - c.used : 0));
            CHECK(p.imageCacheBytes == c.image);
            CHECK(p.coverCacheBytes == c.cover);
            CHECK(p.textureBudgetBytes == c.texture);
            CHECK(p.maxDecodeBytes == c.decode);
        }
    }

    TEST_CASE("RuntimeProfile: application mode settings, and what safe mode turns off") {
        const RuntimeProfile full = makeRuntimeProfile(LaunchMode::Application, kSwitchHeap, kStartUsed);
        CHECK(full.prefetchBehind == 1);
        CHECK(full.prefetchAhead == 3);
        CHECK(full.nextChapterPrefetchPages == 2);
        CHECK(full.backgroundPrefetch);
        CHECK(full.coverRowPrefetch);
        CHECK(full.webtoonTilesAround == 4);
        CHECK(full.stripScreensBehind == 1);  // WP-13b
        CHECK(full.stripScreensAhead == 3);
        CHECK(full.stripZoomPages == 2);  // WP-13d
        // WP-15: Updates and History.
        CHECK(full.updatesPageSize == 50);
        CHECK(full.historyPageSize == 50);
        CHECK(full.updatesFillPages == 5);
        CHECK(full.maxUpdateRows == 2000);
        CHECK(full.maxHistoryRows == 500);
        CHECK(full.libraryUpdatePollMs == 1000);
        CHECK(full.libraryUpdateStartWaitMs == 10000);
        // WP-15b: the waiting-save queue.
        CHECK(full.pendingSavesMax == 500);  // WP-18b (D61): offline reading goes there too
        CHECK(full.progressQueueFileBytes == 256 * 1024);
        // WP-18b: reading away from home.
        CHECK(full.offlineCheckMs == 30000);
        CHECK(full.offlineNetworkPollMs == 2000);
        CHECK(full.offlineSuspectGapMs == 5000);
        CHECK(full.downloadedProgressBatch == 500);
        CHECK(full.switchProgressFileBytes == 512 * 1024);
        CHECK(full.crashExitWaitMs == 5000);  // WP-21 (D62): unused in application mode, the same value
        CHECK(full.httpConcurrency == 6);
        CHECK(full.decodeThreads == 2);
        CHECK(full.maxDecodeScale == doctest::Approx(4.0));  // D48: WP-13c's console measurement
        // The biggest zoomed box (each side at most maxDecodeScale x the screen as held) fits one decode's budget.
        const auto capW = static_cast<std::uint64_t>(full.maxDecodeScale * 1280);
        const auto capH = static_cast<std::uint64_t>(full.maxDecodeScale * 720);
        CHECK(capW * capH * 4 <= full.maxDecodeBytes);
        CHECK(full.heavyUiEffects);
        // S6 input limits and the memory-pressure guard.
        CHECK(full.json.maxBytes == 8 * kMiB);
        CHECK(full.json.maxDepth == 64);
        CHECK(full.maxImageBytes == 32 * kMiB);
        CHECK(full.maxImageSide == 16384);
        CHECK(full.pressurePercent == 85);
        CHECK(full.pressureShrinkPercent == 50);
        CHECK(full.pressureRecoverPercent < full.pressurePercent);
        CHECK(full.minSdFreeBytes == 1 * kGiB);
        // Images (WP-07): tiles fit a texture, every image thread has a stack size, the queue is bounded.
        CHECK(full.maxTextureSide == 2048);
        CHECK(full.shrinkSlackPercent == 15);
        CHECK(full.decodeThreadStackBytes == 1 * kMiB);
        CHECK(full.workerThreadStackBytes == 256 * 1024);
        CHECK(full.imageQueueLimit > 0);
        CHECK(full.imageAuthRetryMs > 0);
        // One decode never needs more than the page cache can hold.
        CHECK(full.maxDecodeBytes <= full.imageCacheBytes);
        // The budgets together leave most of the heap to everything else.
        CHECK(full.imageCacheBytes + full.coverCacheBytes + full.textureBudgetBytes + full.maxDecodeBytes * full.decodeThreads <
              full.freeMem / 2);

        const RuntimeProfile safe = makeRuntimeProfile(LaunchMode::Application, kSwitchHeap, kStartUsed, true);
        CHECK(safe.prefetchBehind == 0);
        CHECK(safe.prefetchAhead == 0);
        CHECK(safe.nextChapterPrefetchPages == 0);
        CHECK_FALSE(safe.backgroundPrefetch);
        CHECK_FALSE(safe.coverRowPrefetch);
        CHECK(safe.webtoonTilesAround == 1);
        CHECK(safe.stripScreensBehind == 0);
        CHECK(safe.stripScreensAhead == 1);
        CHECK(safe.stripZoomPages == 1);
        CHECK(safe.maxZoom == doctest::Approx(2.0));
        CHECK(safe.maxDecodeScale == doctest::Approx(2.0));
        CHECK(safe.httpConcurrency == full.httpConcurrency);
        CHECK(safe.decodeThreads == full.decodeThreads);
    }

    TEST_CASE("RuntimeProfile: Lite, applet mode from the Album (WP-21, D62)") {
        // Other homebrew report a 380-400 MiB heap from the Album; a few dozen MiB are in use at start-up.
        constexpr std::uint64_t kAlbumHeap = 380 * kMiB;
        constexpr std::uint64_t kAlbumUsed = 30 * kMiB;
        const std::uint64_t free = kAlbumHeap - kAlbumUsed;
        const RuntimeProfile lite = makeRuntimeProfile(LaunchMode::Applet, kAlbumHeap, kAlbumUsed);
        CHECK(lite.name == "lite");
        CHECK_FALSE(lite.safeMode);
        CHECK(lite.freeMem == free);
        // Shares of the free memory, with Lite's caps.
        CHECK(lite.imageCacheBytes == 64 * kMiB);            // 25 % would be 87 MiB
        CHECK(lite.coverCacheBytes == 16 * kMiB);            // 5 % would be 17 MiB
        CHECK(lite.textureBudgetBytes == free / 100 * 15);  // 52 MiB
        CHECK(lite.maxDecodeBytes == free / 100 * 10);      // 35 MiB
        CHECK(lite.maxZoom == doctest::Approx(2.0));
        CHECK(lite.maxDecodeScale == doctest::Approx(2.0));
        // The biggest zoomed box fits one decode's budget, even at the floor.
        const auto capW = static_cast<std::uint64_t>(lite.maxDecodeScale * 1280);
        const auto capH = static_cast<std::uint64_t>(lite.maxDecodeScale * 720);
        CHECK(capW * capH * 4 <= lite.maxDecodeBytes);
        CHECK(capW * capH * 4 <= 16 * kMiB);
        // Prefetch: one page each way, nothing of the next chapter.
        CHECK(lite.prefetchBehind == 1);
        CHECK(lite.prefetchAhead == 1);
        CHECK(lite.nextChapterPrefetchPages == 0);
        CHECK_FALSE(lite.backgroundPrefetch);
        CHECK(lite.webtoonTilesAround == 1);
        CHECK(lite.stripScreensBehind == 0);
        CHECK(lite.stripScreensAhead == 1);
        CHECK(lite.stripZoomPages == 1);
        // Work in parallel: borealis's 2 sockets, one decode thread, one source and one page at a time.
        CHECK(lite.httpConcurrency == 2);
        CHECK(lite.decodeThreads == 1);
        CHECK(lite.globalSearchConcurrency == 1);
        CHECK(lite.switchDownloadPagesAtOnce == 1);
        CHECK_FALSE(lite.heavyUiEffects);
        CHECK(lite.crashExitWaitMs == 5000);  // an applet-mode crash waits at most 5 s to be ended
        CHECK(lite.liteMinFreeBytes == 160 * kMiB);
        // The budgets together leave most of the heap to everything else (borealis, the keyboard's share is outside it).
        CHECK(lite.imageCacheBytes + lite.coverCacheBytes + lite.textureBudgetBytes + lite.maxDecodeBytes * lite.decodeThreads <
              lite.freeMem / 2);
        CHECK(lite.maxDecodeBytes <= lite.imageCacheBytes);

        // Every other limit is Full's.
        const RuntimeProfile full = makeRuntimeProfile(LaunchMode::Application, kSwitchHeap, kStartUsed);
        CHECK(lite.coverRowPrefetch == full.coverRowPrefetch);
        CHECK(lite.maxChapters == full.maxChapters);
        CHECK(lite.maxUpdateRows == full.maxUpdateRows);
        CHECK(lite.maxHistoryRows == full.maxHistoryRows);
        CHECK(lite.browseMaxItems == full.browseMaxItems);
        CHECK(lite.maxImageBytes == full.maxImageBytes);  // 32 MiB: one page at a time keeps the peak at one page
        CHECK(lite.maxImageSide == full.maxImageSide);
        CHECK(lite.pressurePercent == full.pressurePercent);
        CHECK(lite.diskCacheBytes == full.diskCacheBytes);  // on the SD card, shared with application mode
        CHECK(lite.switchDownloadLimitBytes == full.switchDownloadLimitBytes);
        CHECK(lite.pendingSavesMax == full.pendingSavesMax);
        CHECK(lite.httpTimeoutMs == full.httpTimeoutMs);
        CHECK(lite.decodeThreadStackBytes == full.decodeThreadStackBytes);
    }

    TEST_CASE("RuntimeProfile: Lite in safe mode halves the budgets, never below the floors") {
        const RuntimeProfile lite = makeRuntimeProfile(LaunchMode::Applet, 380 * kMiB, 30 * kMiB);
        const RuntimeProfile safe = makeRuntimeProfile(LaunchMode::Applet, 380 * kMiB, 30 * kMiB, true);
        CHECK(safe.name == "lite");
        CHECK(safe.safeMode);
        CHECK(safe.imageCacheBytes == 32 * kMiB);
        CHECK(safe.coverCacheBytes == 8 * kMiB);
        CHECK(safe.textureBudgetBytes == 32 * kMiB);  // half would be 26 MiB: the floor
        CHECK(safe.maxDecodeBytes == lite.maxDecodeBytes / 2);
        CHECK(safe.prefetchBehind == 0);
        CHECK(safe.prefetchAhead == 0);
        CHECK_FALSE(safe.coverRowPrefetch);
        CHECK(safe.maxDecodeScale == doctest::Approx(2.0));
        CHECK(safe.httpConcurrency == 2);
        CHECK(safe.crashExitWaitMs == 5000);
        // Nothing known: the floors.
        const RuntimeProfile tiny = makeRuntimeProfile(LaunchMode::Applet, 0, 0, true);
        CHECK(tiny.imageCacheBytes == 24 * kMiB);
        CHECK(tiny.coverCacheBytes == 8 * kMiB);
        CHECK(tiny.textureBudgetBytes == 32 * kMiB);
        CHECK(tiny.maxDecodeBytes == 16 * kMiB);
        CHECK(tiny.prefetchAhead == 0);
    }

    TEST_CASE("RuntimeProfile: too little memory for Lite (the low-memory screen, WP-21)") {
        // Free at start = total - used, compared with liteMinFreeBytes (160 MiB).
        CHECK_FALSE(tooLittleMemoryForLite(LaunchMode::Applet, makeRuntimeProfile(LaunchMode::Applet, 380 * kMiB, 30 * kMiB)));
        CHECK_FALSE(tooLittleMemoryForLite(LaunchMode::Applet, makeRuntimeProfile(LaunchMode::Applet, 170 * kMiB, 10 * kMiB)));
        CHECK(tooLittleMemoryForLite(LaunchMode::Applet, makeRuntimeProfile(LaunchMode::Applet, 170 * kMiB, 11 * kMiB)));
        CHECK(tooLittleMemoryForLite(LaunchMode::Applet, makeRuntimeProfile(LaunchMode::Applet, 100 * kMiB, 5 * kMiB)));
        CHECK(tooLittleMemoryForLite(LaunchMode::Applet, makeRuntimeProfile(LaunchMode::Applet, 0, 0)));
        CHECK(tooLittleMemoryForLite(LaunchMode::Applet, makeRuntimeProfile(LaunchMode::Applet, 100 * kMiB, 200 * kMiB)));
        CHECK(tooLittleMemoryForLite(LaunchMode::Applet, makeRuntimeProfile(LaunchMode::Applet, 100 * kMiB, 5 * kMiB, true)));
        // Application mode never: there the memory-pressure guard copes.
        CHECK_FALSE(tooLittleMemoryForLite(LaunchMode::Application, makeRuntimeProfile(LaunchMode::Application, 0, 0)));
        CHECK_FALSE(tooLittleMemoryForLite(LaunchMode::Application,
                                           makeRuntimeProfile(LaunchMode::Application, 100 * kMiB, 5 * kMiB)));
    }

    TEST_CASE("RuntimeProfile: pure, the same input gives the same profile") {
        const RuntimeProfile a = makeRuntimeProfile(LaunchMode::Application, 777 * kMiB, 123 * kMiB);
        const RuntimeProfile b = makeRuntimeProfile(LaunchMode::Application, 777 * kMiB, 123 * kMiB);
        CHECK(a.imageCacheBytes == b.imageCacheBytes);
        CHECK(a.coverCacheBytes == b.coverCacheBytes);
        CHECK(a.textureBudgetBytes == b.textureBudgetBytes);
        CHECK(a.maxDecodeBytes == b.maxDecodeBytes);
    }
}

TEST_SUITE("core") {
    TEST_CASE("RetryPolicy: doubling waits with a cap, only idempotent + transient + retries left") {
        RetryPolicy policy;  // 2 retries, 250 ms, cap 2 s
        CHECK(retryDelayMs(policy, 0) == 0);
        CHECK(retryDelayMs(policy, 1) == 250);
        CHECK(retryDelayMs(policy, 2) == 500);
        CHECK(retryDelayMs(policy, 4) == 2000);
        CHECK(retryDelayMs(policy, 60) == 2000);
        CHECK(shouldRetry(policy, 0, true, true));
        CHECK(shouldRetry(policy, 1, true, true));
        CHECK_FALSE(shouldRetry(policy, 2, true, true));
        CHECK_FALSE(shouldRetry(policy, 0, false, true));
        CHECK_FALSE(shouldRetry(policy, 0, true, false));
        CHECK(isTransientHttpStatus(503));
        CHECK(isTransientHttpStatus(429));
        CHECK_FALSE(isTransientHttpStatus(500));
        CHECK_FALSE(isTransientHttpStatus(404));
        CHECK(makeRuntimeProfile(LaunchMode::Application, 4 * kGiB, 0).httpRetry.maxRetries == 2);
    }
}
