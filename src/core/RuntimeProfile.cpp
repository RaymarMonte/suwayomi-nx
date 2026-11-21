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
        // 256 MiB at most (WP-12): in the soak, 512 filled up with pages the reader never came back to (D45).
        p.imageCacheBytes = share(p.freeMem, 40, kImageFloor, 256 * kMiB);
        p.coverCacheBytes = share(p.freeMem, 5, kCoverFloor, 64 * kMiB);
        p.textureBudgetBytes = share(p.freeMem, 15, kTextureFloor, 256 * kMiB);
        p.maxDecodeBytes = share(p.freeMem, 10, kDecodeFloor, 128 * kMiB);
    } else {
        // Lite (WP-21, D62): from the Album, with a twelfth of the memory: 259.7 MiB on the console (the first guesses were
        // for 380). There the budgets come to 48 + 13 + 39 + 26 MiB, under half the heap: with borealis (~45 MiB) and one
        // download page they stay below the pressure line (85 %). Every limit not set here is Full's.
        p.name = "lite";
        p.imageCacheBytes = share(p.freeMem, 25, kImageFloor, 48 * kMiB);
        p.coverCacheBytes = share(p.freeMem, 5, kCoverFloor, 16 * kMiB);
        p.textureBudgetBytes = share(p.freeMem, 15, kTextureFloor, 64 * kMiB);
        p.maxDecodeBytes = share(p.freeMem, 10, kDecodeFloor, 64 * kMiB);
        p.maxDecodeScale = 2.0f;  // ARCHITECTURE.md's Lite column; 1 made zoomed pages blurry
        p.maxZoom = 2.0f;
        p.prefetchAhead = 1;  // -1 / +1 pages
        p.nextChapterPrefetchPages = 0;
        p.backgroundPrefetch = false;
        p.webtoonTilesAround = 1;
        p.stripScreensBehind = 0;
        p.stripScreensAhead = 1;
        p.stripZoomPages = 1;
        p.globalSearchConcurrency = 1;
        p.switchDownloadPagesAtOnce = 1;  // one page at a time (D57): the peak stays at one page
        p.httpConcurrency = 2;            // borealis gives applet mode 2 sockets
        p.decodeThreads = 1;
        p.heavyUiEffects = false;
    }

    if (safeMode) {
        p.safeMode = true;
        p.imageCacheBytes = halved(p.imageCacheBytes, kImageFloor);
        p.coverCacheBytes = halved(p.coverCacheBytes, kCoverFloor);
        p.textureBudgetBytes = halved(p.textureBudgetBytes, kTextureFloor);
        p.maxDecodeBytes = halved(p.maxDecodeBytes, kDecodeFloor);
        p.maxZoom = std::min(p.maxZoom, 2.0f);
        p.maxDecodeScale = std::min(p.maxDecodeScale, 2.0f);  // after a crash: the smaller zoomed decodes of before WP-13c
        p.prefetchBehind = 0;
        p.prefetchAhead = 0;
        p.nextChapterPrefetchPages = 0;
        p.backgroundPrefetch = false;
        p.coverRowPrefetch = false;
        p.webtoonTilesAround = 1;
        p.stripScreensBehind = 0;
        p.stripScreensAhead = 1;
        p.stripZoomPages = 1;
        p.switchDownloadPagesAtOnce = 1;
    }
    return p;
}

bool tooLittleMemoryForLite(LaunchMode mode, const RuntimeProfile& profile) {
    return mode == LaunchMode::Applet && profile.freeMem < profile.liteMinFreeBytes;
}

}  // namespace snx::core
