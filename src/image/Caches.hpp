#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "core/ByteLru.hpp"
#include "core/ImageGeometry.hpp"
#include "image/DecodedImage.hpp"

// The memory side of the image caches (WP-07): decoded images kept in RAM, and the GPU texture budget.
namespace snx::image {

// The memory-cache key: the same URL decoded for another box is another image. "url|1280x0".
std::string cacheKey(std::string_view url, core::ImageSize box);

// Decoded images in RAM, least recently used out first, within a byte budget (RuntimeProfile::imageCacheBytes for pages,
// coverCacheBytes for covers). Any thread; one short lock per call. An image the UI still holds stays alive after
// eviction (shared pointer); the cache just stops counting it.
class MemoryCache {
  public:
    explicit MemoryCache(std::uint64_t budget) : lru_(budget) {}

    ImagePtr get(const std::string& key);  // counts as a use
    void put(const std::string& key, ImagePtr image);
    void setBudget(std::uint64_t budget);  // evicts down to it at once (memory pressure)
    void clear();
    std::uint64_t bytes() const;
    std::uint64_t budget() const;
    std::size_t size() const;

  private:
    mutable std::mutex mutex_;
    core::ByteLru<std::string, ImagePtr> lru_;
};

// GPU memory accounting (SAFETY.md S7): before the UI creates a texture it reserves the bytes here, and gives them back when it
// deletes the texture. A refused reservation means "don't create it": show a placeholder or free textures far from the screen
// first. Main thread only, like the textures themselves (RuntimeProfile::textureBudgetBytes).
class TextureBudget {
  public:
    explicit TextureBudget(std::uint64_t budget) : budget_(budget) {}

    static std::uint64_t bytesFor(const core::TileRect& tile) { return core::rgbaBytes({tile.width, tile.height}); }

    bool tryReserve(std::uint64_t bytes);
    void release(std::uint64_t bytes);
    std::uint64_t used() const { return used_; }
    std::uint64_t budget() const { return budget_; }

  private:
    std::uint64_t budget_;
    std::uint64_t used_ = 0;
};

// One reservation that gives itself back when it goes away (keep it next to the texture handle).
class TextureLease {
  public:
    TextureLease() = default;
    TextureLease(TextureBudget& budget, std::uint64_t bytes);  // check ok() afterwards
    TextureLease(TextureLease&& other) noexcept;
    TextureLease& operator=(TextureLease&& other) noexcept;
    TextureLease(const TextureLease&) = delete;
    TextureLease& operator=(const TextureLease&) = delete;
    ~TextureLease() { reset(); }

    bool ok() const { return budget_ != nullptr; }
    void reset();

  private:
    TextureBudget* budget_ = nullptr;
    std::uint64_t bytes_ = 0;
};

}  // namespace snx::image
