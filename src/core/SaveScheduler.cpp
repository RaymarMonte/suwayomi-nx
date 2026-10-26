#include "core/SaveScheduler.hpp"

namespace snx::core {

SaveScheduler::SaveScheduler(int delayMs) : delayMs_(delayMs < 0 ? 0 : delayMs) {}

void SaveScheduler::changed(std::int64_t nowMs) {
    if (!pending_) {
        pending_ = true;
        firstChangeMs_ = nowMs;
    }
    lastChangeMs_ = nowMs;
}

bool SaveScheduler::due(std::int64_t nowMs) const {
    if (!pending_) {
        return false;
    }
    const std::int64_t quiet = nowMs - lastChangeMs_;
    const std::int64_t waited = nowMs - firstChangeMs_;
    return quiet >= delayMs_ || waited >= 4 * static_cast<std::int64_t>(delayMs_);
}

void SaveScheduler::saved() {
    pending_ = false;
}

}  // namespace snx::core
