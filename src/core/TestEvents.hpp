#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "core/RuntimeInfo.hpp"

// The pure part of the test hooks (docs/TESTING.md "Test hooks inside the app"): how one event line looks, and when the app
// counts as idle. The app's TestHooks (src/app/) owns the file and the clock and calls these.
namespace snx::core::testevents {

// One JSON Lines entry: {"t":<ms since start>,"ev":"<name>",<fields...>} plus "\n". Fields named "t" or "ev" are dropped,
// so they can't overwrite the two that every event has.
std::string line(std::int64_t tMs, std::string_view ev, const nlohmann::ordered_json& fields = nlohmann::ordered_json::object());

// What the app_start event reports.
struct AppStart {
    std::string_view version;   // "0.0.1"
    std::string_view build;     // "test" or "release"
    std::string_view platform;  // "switch" or "desktop"
    RuntimeInfo runtime;
    std::string_view profile;   // "full" in application mode. "none" in applet mode, which gets no profile (D21)
    bool safeMode = false;      // the last run crashed, so this one runs in safe mode (SAFETY.md S1)
    std::string_view lastCrash; // the crash record behind it ("logs/crashes/crash-....txt"), empty if none
};

std::string appStart(std::int64_t tMs, const AppStart& info);
std::string screen(std::int64_t tMs, std::string_view name);
std::string idle(std::int64_t tMs);
// {"ev":"drill","name":"null-write","phase":"start"}: a crash drill starts (phase "start"), or the cache-write drill is
// writing (phase "writing").
std::string drill(std::int64_t tMs, std::string_view name, std::string_view phase);

// Decides when to write an "idle" event: after kQuietFrames frames in a row with nothing running (no animation; later also no
// network or decode job). One idle event per quiet spell: it comes again only after something was busy, or after markBusy().
class IdleTracker {
  public:
    static constexpr int kQuietFrames = 3;

    // Call once per frame. Returns true on the one frame where the app has just become idle.
    bool onFrame(bool busy);

    // Something changed that a test will wait on (a new screen): the next quiet spell reports idle again.
    void markBusy();

  private:
    int quietFrames_ = 0;
    bool reported_ = false;
};

}  // namespace snx::core::testevents
