#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "core/ByteLru.hpp"
#include "core/RuntimeProfile.hpp"
#include "platform/SafeFs.hpp"

namespace snx::image {

struct DiskCacheOptions {
    std::string dir = "cache/images";    // inside the app's folder (D10); 256 sub-folders 00..ff below it
    std::uint64_t budgetBytes = 0;       // RuntimeProfile::diskCacheBytes
    std::uint64_t minFreeBytes = 0;      // RuntimeProfile::minSdFreeBytes: never write if the SD card would drop below this
    std::uint64_t maxEntryBytes = 0;     // RuntimeProfile::maxImageBytes: bigger files aren't cached (or read back)
    // How much space is free; default: SafeFs::freeBytes(). Tests put a fake here.
    std::function<std::optional<std::uint64_t>()> freeBytes;
};
DiskCacheOptions diskCacheOptionsFrom(const core::RuntimeProfile& profile);

// Downloaded image files on the SD card (the PC: the data folder), so a page seen once loads without the network (WP-07).
// - One file per URL: <dir>/<first 2 hex>/<16 hex of the URL's FNV-1a hash>.img, holding a small header (the URL, the
//   length, a checksum) and the bytes as downloaded. Written once through SafeFs::writeAtomic (S5), never edited.
// - A file that is cut short, has the wrong checksum or a stranger's URL counts as missing (and is deleted, except a
//   stranger's). Leftover .tmp files and anything else that isn't ours go at the first scan.
// - Size budget: least recently used files go first. After a restart the order comes from the files' times, then from use.
// - Free-space guard (S5): no write if the free space would drop below minFreeBytes.
// - stop(): no more writes (exit, HOME, sleep). Reading still works.
// The first read or write scans the folder (slow on an SD card: call it from a worker thread). Thread-safe, but meant to be
// used from one worker thread (the loader's disk thread).
class DiskCache {
  public:
    DiskCache(const platform::SafeFs& fs, DiskCacheOptions options);

    std::optional<std::string> read(std::string_view url);

    enum class Write { Written, AlreadyThere, TooLarge, NoSpace, Failed, Stopped };
    Write write(std::string_view url, std::string_view bytes);

    void stop();
    void scan();  // done by the first read/write anyway; tests call it to restart the counting

    std::uint64_t bytes();   // what the cached files take up
    std::size_t count();

    static std::string keyOf(std::string_view url);  // 16 lower-case hex digits
    std::string pathOf(const std::string& key) const;

  private:
    void scanLocked();

    const platform::SafeFs& fs_;
    DiskCacheOptions options_;
    std::mutex mutex_;
    bool scanned_ = false;
    bool stopped_ = false;
    core::ByteLru<std::string, char> index_;  // key -> nothing; the bytes are the file sizes
};

}  // namespace snx::image
