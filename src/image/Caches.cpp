#include "image/Caches.hpp"

#include <utility>

namespace snx::image {

std::string cacheKey(std::string_view url, core::ImageSize box) {
    return std::string(url) + "|" + std::to_string(box.width) + "x" + std::to_string(box.height);
}

ImagePtr MemoryCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ImagePtr* found = lru_.get(key);
    return found != nullptr ? *found : nullptr;
}

void MemoryCache::put(const std::string& key, ImagePtr image) {
    if (!image) {
        return;
    }
    const std::uint64_t bytes = image->bytes();
    std::lock_guard<std::mutex> lock(mutex_);
    lru_.put(key, std::move(image), bytes);
}

void MemoryCache::setBudget(std::uint64_t budget) {
    std::lock_guard<std::mutex> lock(mutex_);
    lru_.setBudget(budget);
}

void MemoryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lru_.clear();
}

std::uint64_t MemoryCache::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lru_.bytes();
}

std::uint64_t MemoryCache::budget() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lru_.budget();
}

std::size_t MemoryCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lru_.size();
}

bool TextureBudget::tryReserve(std::uint64_t bytes) {
    if (bytes > budget_ - used_ || used_ > budget_) {
        return false;
    }
    used_ += bytes;
    return true;
}

void TextureBudget::release(std::uint64_t bytes) {
    used_ = bytes > used_ ? 0 : used_ - bytes;
}

TextureLease::TextureLease(TextureBudget& budget, std::uint64_t bytes) {
    if (budget.tryReserve(bytes)) {
        budget_ = &budget;
        bytes_ = bytes;
    }
}

TextureLease::TextureLease(TextureLease&& other) noexcept
    : budget_(std::exchange(other.budget_, nullptr)), bytes_(std::exchange(other.bytes_, 0)) {}

TextureLease& TextureLease::operator=(TextureLease&& other) noexcept {
    if (this != &other) {
        reset();
        budget_ = std::exchange(other.budget_, nullptr);
        bytes_ = std::exchange(other.bytes_, 0);
    }
    return *this;
}

void TextureLease::reset() {
    if (budget_ != nullptr) {
        budget_->release(bytes_);
        budget_ = nullptr;
        bytes_ = 0;
    }
}

}  // namespace snx::image
