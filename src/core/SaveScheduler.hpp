#pragma once

#include <cstdint>

// When to save a file that changes now and then, such as config.json (SAFETY.md S5: few, debounced writes; WP-08).
// After a change the save waits until nothing changed for delayMs, so a burst of changes (typing, a token renewal right
// after a login) becomes one write. A steady stream of changes can't hold it back forever: at most 4 x delayMs after the
// first unsaved change it is due anyway. Pure and not thread-safe: the app calls it on the main thread with its own clock.
namespace snx::core {

class SaveScheduler {
  public:
    explicit SaveScheduler(int delayMs);

    void changed(std::int64_t nowMs);
    bool pending() const { return pending_; }
    bool due(std::int64_t nowMs) const;
    // The file was written (or there was nothing to write).
    void saved();

  private:
    int delayMs_;
    bool pending_ = false;
    std::int64_t firstChangeMs_ = 0;
    std::int64_t lastChangeMs_ = 0;
};

}  // namespace snx::core
