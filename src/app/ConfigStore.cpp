#include "app/ConfigStore.hpp"

#include <borealis.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>

#include "app/TestHooks.hpp"

namespace snx::app {

namespace {

constexpr const char* kConfigPath = "config.json";
constexpr const char* kBackupPath = "config.json.bak";  // SafeFs::writeAtomic's Backup::Keep name

}  // namespace

std::int64_t appClockMs() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

ConfigStore::ConfigStore(const platform::SafeFs& fs, const core::RuntimeProfile& profile)
    : fs_(fs), limits_(profile.json), scheduler_(profile.configSaveDelayMs) {}

void ConfigStore::load() {
    const auto text = fs_.readFile(kConfigPath, limits_.maxBytes);
    core::ConfigLoad loaded = core::loadAppConfig(text ? *text : std::string(), limits_);
    // A missing file is the first run. A file that isn't usable JSON (cut short by a power cut on exFAT) is what the .bak is for.
    const bool unusable = !text || (!loaded.problems.empty() && loaded.config.server.url.empty());
    if (unusable) {
        if (const auto backup = fs_.readFile(kBackupPath, limits_.maxBytes)) {
            core::ConfigLoad fromBackup = core::loadAppConfig(*backup, limits_);
            if (!fromBackup.config.server.url.empty()) {
                brls::Logger::warning("config: {} is {}, using {}", kConfigPath, text ? "unreadable" : "missing", kBackupPath);
                loaded = std::move(fromBackup);
            }
        }
    }
    for (const std::string& problem : loaded.problems) {
        brls::Logger::warning("config: {}", problem);
    }
    config_ = std::move(loaded.config);
    brls::Logger::info("config: server {}, login {}", config_.server.url.empty() ? "not set up" : config_.server.url,
                       std::string(core::authModeName(config_.server.auth)));
}

void ConfigStore::setServer(const core::ServerSettings& server) {
    config_.server = server;
    scheduler_.changed(appClockMs());
}

void ConfigStore::setLibrary(const core::LibrarySettings& library) {
    if (library.categoryId == config_.library.categoryId && library.sort == config_.library.sort) {
        return;
    }
    config_.library = library;
    scheduler_.changed(appClockMs());
}

void ConfigStore::useServer(const core::ServerSettings& server) {
    config_.server = server;
}

void ConfigStore::onFrame() {
    if (scheduler_.due(appClockMs())) {
        save();
    }
}

bool ConfigStore::flush() {
    return !scheduler_.pending() || save();
}

bool ConfigStore::save() {
    const std::string text = core::saveAppConfig(config_);
    if (!fs_.writeAtomic(kConfigPath, text, platform::SafeFs::Backup::Keep)) {
        brls::Logger::error("config: couldn't save {}", kConfigPath);
        scheduler_.changed(appClockMs());  // try again after the next delay
        return false;
    }
    scheduler_.saved();
    brls::Logger::info("config: saved ({} bytes)", text.size());
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["bytes"] = text.size();
    fields["server"] = config_.server.url;
    fields["auth"] = std::string(core::authModeName(config_.server.auth));
    fields["hasToken"] = !config_.server.refreshToken.empty();
    testhooks::write("config_saved", fields);
    return true;
}

}  // namespace snx::app
