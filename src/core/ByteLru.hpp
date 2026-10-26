#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

// A "least recently used" cache with a budget in bytes (ARCHITECTURE.md "core" policies, WP-07). The image module keeps decoded
// pages and covers in one each, and the disk cache's index is one too. Each item says how many bytes it counts for; when the
// total goes over the budget, the items used longest ago go first. Not thread-safe: the owner holds a lock around it.
namespace snx::core {

template <typename Key, typename Value>
class ByteLru {
  public:
    explicit ByteLru(std::uint64_t budget = 0) : budget_(budget) {}

    // Adds the item (or replaces the one with the same key) as the most recently used, then evicts down to the budget.
    // An item bigger than the whole budget isn't kept (false). Evicted keys are added to `evicted` if given.
    bool put(const Key& key, Value value, std::uint64_t bytes, std::vector<Key>* evicted = nullptr) {
        erase(key);
        if (bytes > budget_) {
            return false;
        }
        order_.push_back(Entry{key, std::move(value), bytes});
        index_[key] = std::prev(order_.end());
        bytes_ += bytes;
        shrinkTo(budget_, evicted);
        return true;
    }

    // The item, now the most recently used. nullptr if it isn't there. The pointer is valid until the next change.
    Value* get(const Key& key) {
        const auto found = index_.find(key);
        if (found == index_.end()) {
            return nullptr;
        }
        order_.splice(order_.end(), order_, found->second);
        return &found->second->value;
    }

    // Like get(), but doesn't count as a use.
    const Value* peek(const Key& key) const {
        const auto found = index_.find(key);
        return found == index_.end() ? nullptr : &found->second->value;
    }

    bool contains(const Key& key) const { return index_.count(key) > 0; }

    bool erase(const Key& key) {
        const auto found = index_.find(key);
        if (found == index_.end()) {
            return false;
        }
        bytes_ -= found->second->bytes;
        order_.erase(found->second);
        index_.erase(found);
        return true;
    }

    // Evicts the least recently used items until at most `target` bytes are left. Returns how many went.
    std::size_t shrinkTo(std::uint64_t target, std::vector<Key>* evicted = nullptr) {
        std::size_t count = 0;
        while (bytes_ > target && !order_.empty()) {
            if (evicted != nullptr) {
                evicted->push_back(order_.front().key);
            }
            bytes_ -= order_.front().bytes;
            index_.erase(order_.front().key);
            order_.pop_front();
            ++count;
        }
        return count;
    }

    // A new budget; evicts down to it at once.
    void setBudget(std::uint64_t budget, std::vector<Key>* evicted = nullptr) {
        budget_ = budget;
        shrinkTo(budget_, evicted);
    }

    void clear() {
        order_.clear();
        index_.clear();
        bytes_ = 0;
    }

    std::uint64_t bytes() const { return bytes_; }
    std::uint64_t budget() const { return budget_; }
    std::size_t size() const { return order_.size(); }

    // Least recently used first: the order items would be evicted in.
    std::vector<Key> keysOldestFirst() const {
        std::vector<Key> keys;
        keys.reserve(order_.size());
        for (const Entry& entry : order_) {
            keys.push_back(entry.key);
        }
        return keys;
    }

  private:
    struct Entry {
        Key key;
        Value value;
        std::uint64_t bytes = 0;
    };

    std::uint64_t budget_ = 0;
    std::uint64_t bytes_ = 0;
    std::list<Entry> order_;  // front = least recently used
    std::unordered_map<Key, typename std::list<Entry>::iterator> index_;
};

}  // namespace snx::core
