#pragma once

#include <cstddef>
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
    // The library's category and sort as the user left them (WP-09). Saved a moment later, if they changed.
    void setLibrary(const core::LibrarySettings& library);
    // WP-11: a manga's own reading direction and fit (at most RuntimeProfile::readerMangaSettingsMax manga are remembered),
    // and the reader's background and page number (for every manga). Saved a moment later, if they changed.
    void setMangaReader(const core::MangaReaderSettings& settings);
    void setReaderLook(core::ReaderBackground background, bool showPageNumber);
    // How the console is held while reading, and the portrait way the quick rotate goes to (WP-22).
    void setReaderOrientation(core::ReaderOrientation orientation, core::ReaderOrientation lastPortrait);
    // What a page turn does to a zoomed page (WP-13), for every manga.
    void setReaderZoomOnTurn(core::ZoomOnTurn mode);
    // When spreads show as two halves (WP-13c), for every manga.
    void setReaderSplitSpreads(core::SpreadSplit mode);
    // The webtoon strip's width in landscape and its own orientation (WP-13b), for every manga.
    void setReaderStripWidth(core::StripWidth width);
    void setReaderStripOrientation(core::ReaderOrientation orientation);
    // The test config's server (test builds): used as if it had been saved, but not written unless something changes it.
    void useServer(const core::ServerSettings& server);

    // Once per main-loop iteration: saves when the debounce is over, unless postponeSaveThisFrame() was called in it.
    void onFrame();
    // The reader calls this in every frame in which the page or the strip moves: a save that is due then waits for the first
    // frame without the call (writing to the SD card can take longer than a frame: a hitch in a scroll, WP-13b). It clears
    // itself each frame, so it can't hold saves back once the reader stops calling it. flush() still saves at once.
    void postponeSaveThisFrame() { postponeThisFrame_ = true; }
    // The reader called postponeSaveThisFrame() in this frame (read before onFrame(), which clears it): the waiting-save
    // queue's file waits for a still frame the same way (WP-15b).
    bool postponedThisFrame() const { return postponeThisFrame_; }
    // Saves now if anything changed. True if nothing needed saving or the save worked.
    bool flush();

  private:
    bool save();

    const platform::SafeFs& fs_;
    core::JsonLimits limits_;
    std::size_t mangaReaderMax_;
    core::AppConfig config_;
    core::SaveScheduler scheduler_;
    bool postponeThisFrame_ = false;
};

// Milliseconds on a steady clock, the app's one clock for timers (logger, config saves, test events).
std::int64_t appClockMs();

}  // namespace snx::app
