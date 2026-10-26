#pragma once

#include <cstdint>

#include "core/AppConfig.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/SaveScheduler.hpp"
#include "platform/SafeFs.hpp"

// config.json (D10, D36) for the running app (WP-08). Loads it once at start (falling back to config.json.bak when the file is
// missing or unreadable), keeps the settings in memory, and saves them the safe way (SAFETY.md S5): SafeFs::writeAtomic with
// a .bak of the previous file, debounced (core::SaveScheduler, RuntimeProfile::configSaveDelayMs), and right away at exit,
// HOME and sleep (flush). Main thread only.
namespace snx::app {

class ConfigStore {
  public:
    ConfigStore(const platform::SafeFs& fs, const core::RuntimeProfile& profile);

    // Reads config.json, or config.json.bak if config.json is missing or isn't readable JSON. Never fails: defaults otherwise.
    void load();

    const core::AppConfig& config() const { return config_; }
    bool serverSetUp() const { return !config_.server.url.empty(); }

    // New server settings (setup, login, a renewed token, logout). Saved a moment later.
    void setServer(const core::ServerSettings& server);
    // The test config's server (test builds): used as if it had been saved, but not written unless something changes it.
    void useServer(const core::ServerSettings& server);

    // Once per main-loop iteration: saves when the debounce is over.
    void onFrame();
    // Saves now if anything changed. True if nothing needed saving or the save worked.
    bool flush();

  private:
    bool save();

    const platform::SafeFs& fs_;
    core::JsonLimits limits_;
    core::AppConfig config_;
    core::SaveScheduler scheduler_;
};

// Milliseconds on a steady clock, the app's one clock for timers (logger, config saves, test events).
std::int64_t appClockMs();

}  // namespace snx::app
