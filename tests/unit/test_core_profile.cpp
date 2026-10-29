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
        CHECK(full.httpConcurrency == 6);
        CHECK(full.decodeThreads == 2);
        CHECK(full.maxDecodeScale == doctest::Approx(2.0));
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
        CHECK(safe.httpConcurrency == full.httpConcurrency);
        CHECK(safe.decodeThreads == full.decodeThreads);
    }

    TEST_CASE("RuntimeProfile: applet mode still gets something small and safe (the app exits before using it, D21)") {
        const RuntimeProfile applet = makeRuntimeProfile(LaunchMode::Applet, 400 * kMiB, 30 * kMiB);
        CHECK(applet.name == "applet");
        CHECK(applet.httpConcurrency == 2);
        CHECK(applet.decodeThreads == 1);
        CHECK(applet.prefetchAhead == 1);
        CHECK_FALSE(applet.backgroundPrefetch);
        CHECK(applet.imageCacheBytes <= 64 * kMiB);
        CHECK(applet.maxDecodeBytes <= 64 * kMiB);
        CHECK(applet.maxDecodeScale == doctest::Approx(1.0));
        CHECK_FALSE(applet.heavyUiEffects);
        const RuntimeProfile tiny = makeRuntimeProfile(LaunchMode::Applet, 0, 0, true);
        CHECK(tiny.imageCacheBytes == 24 * kMiB);
        CHECK(tiny.prefetchAhead == 0);
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
