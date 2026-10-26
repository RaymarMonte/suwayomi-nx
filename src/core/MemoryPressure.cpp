#include "core/MemoryPressure.hpp"

#include <algorithm>

namespace snx::core {

MemoryPressure::MemoryPressure(const RuntimeProfile& profile)
    : pressurePercent_(std::clamp(profile.pressurePercent, 1, 100)),
      recoverPercent_(std::clamp(profile.pressureRecoverPercent, 0, std::clamp(profile.pressurePercent, 1, 100) - 1)),
      shrinkPercent_(std::clamp(profile.pressureShrinkPercent, 0, 100)) {}

MemoryPressure::Change MemoryPressure::update(MemorySample sample) {
    if (sample.total == 0) {
        return Change::None;
    }
    // In doubles, so huge numbers can't overflow; used over total counts as 100 %.
    const std::uint64_t used = std::min(sample.used, sample.total);
    usedPercent_ = static_cast<int>(static_cast<double>(used) * 100.0 / static_cast<double>(sample.total));
    if (!under_ && usedPercent_ >= pressurePercent_) {
        under_ = true;
        return Change::Entered;
    }
    if (under_ && usedPercent_ < recoverPercent_) {
        under_ = false;
        return Change::Left;
    }
    return Change::None;
}

}  // namespace snx::core
