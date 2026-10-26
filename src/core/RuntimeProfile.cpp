#include "core/RuntimeProfile.hpp"

#include <algorithm>

namespace snx::core {

namespace {

// percent of free, kept between floor and cap.
std::uint64_t share(std::uint64_t free, int percent, std::uint64_t floor, std::uint64_t cap) {
    // free / 100 * percent can't overflow (free < 2^64 / 100 * 100); the remainder is noise at these sizes.
    const std::uint64_t part = free / 100 * static_cast<std::uint64_t>(percent);
    return std::clamp(part, floor, cap);
}

std::uint64_t halved(std::uint64_t bytes, std::uint64_t floor) {
    return std::max(bytes / 2, floor);
}

}  // namespace

RuntimeProfile makeRuntimeProfile(LaunchMode mode, std::uint64_t totalMem, std::uint64_t usedMem, bool safeMode) {
    constexpr std::uint64_t kImageFloor = 24 * kMiB;
    constexpr std::uint64_t kCoverFloor = 8 * kMiB;
    constexpr std::uint64_t kTextureFloor = 32 * kMiB;
    constexpr std::uint64_t kDecodeFloor = 16 * kMiB;

    RuntimeProfile p;
    p.totalMem = totalMem;
    p.freeMem = totalMem > usedMem ? totalMem - usedMem : 0;

    if (mode == LaunchMode::Application) {
        p.name = "full";
        p.imageCacheBytes = share(p.freeMem, 40, kImageFloor, 512 * kMiB);
        p.coverCacheBytes = share(p.freeMem, 5, kCoverFloor, 64 * kMiB);
        p.textureBudgetBytes = share(p.freeMem, 15, kTextureFloor, 256 * kMiB);
        p.maxDecodeBytes = share(p.freeMem, 10, kDecodeFloor, 128 * kMiB);
    } else {
        // Not used (D21): small and safe, roughly ARCHITECTURE.md's Lite column.
        p.name = "applet";
        p.imageCacheBytes = share(p.freeMem, 25, kImageFloor, 64 * kMiB);
        p.coverCacheBytes = share(p.freeMem, 5, kCoverFloor, 16 * kMiB);
        p.textureBudgetBytes = share(p.freeMem, 15, kTextureFloor, 64 * kMiB);
        p.maxDecodeBytes = share(p.freeMem, 10, kDecodeFloor, 64 * kMiB);
        p.maxDecodeScale = 1.0f;
        p.prefetchAhead = 1;
        p.nextChapterPrefetchPages = 0;
        p.backgroundPrefetch = false;
        p.webtoonTilesAround = 1;
        p.coverRowPrefetch = false;
        p.httpConcurrency = 2;
        p.decodeThreads = 1;
        p.heavyUiEffects = false;
    }

    if (safeMode) {
        p.safeMode = true;
        p.imageCacheBytes = halved(p.imageCacheBytes, kImageFloor);
        p.coverCacheBytes = halved(p.coverCacheBytes, kCoverFloor);
        p.textureBudgetBytes = halved(p.textureBudgetBytes, kTextureFloor);
        p.maxDecodeBytes = halved(p.maxDecodeBytes, kDecodeFloor);
        p.prefetchBehind = 0;
        p.prefetchAhead = 0;
        p.nextChapterPrefetchPages = 0;
        p.backgroundPrefetch = false;
        p.coverRowPrefetch = false;
        p.webtoonTilesAround = 1;
    }
    return p;
}

}  // namespace snx::core
