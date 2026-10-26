#pragma once

#include <cstdint>

#include "core/RuntimeProfile.hpp"

// The decision part of the memory-pressure guard (ARCHITECTURE.md "Memory-pressure guard", WP-07). Someone measures memory
// every RuntimeProfile::pressureCheckMs (the image loader does) and feeds the numbers in here. Above pressurePercent of the
// total in use, the app is "under pressure": caches shrink to pressureShrinkPercent and prefetch pauses. It stays that way
// until use drops below pressureRecoverPercent, so it doesn't flip back and forth on every check. Pure, not thread-safe.
namespace snx::core {

struct MemorySample {
    std::uint64_t used = 0;
    std::uint64_t total = 0;
};

class MemoryPressure {
  public:
    enum class Change { None, Entered, Left };

    explicit MemoryPressure(const RuntimeProfile& profile);

    // A new measurement. Entered / Left on the one sample where the state changes. A sample with total 0 changes nothing.
    Change update(MemorySample sample);

    bool underPressure() const { return under_; }
    int usedPercent() const { return usedPercent_; }  // of the last useful sample
    int shrinkPercent() const { return shrinkPercent_; }

  private:
    int pressurePercent_;
    int recoverPercent_;
    int shrinkPercent_;
    bool under_ = false;
    int usedPercent_ = 0;
};

}  // namespace snx::core
