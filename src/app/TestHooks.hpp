#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "core/RuntimeInfo.hpp"
#include "image/ImageLoader.hpp"
#include "net/HttpClient.hpp"
#include "platform/SafeFs.hpp"

// Test events (docs/TESTING.md "Test hooks inside the app"). Test builds (SNX_TEST_HOOKS) write one JSON line per event to
// <data>/test/events.jsonl, and the E2E harness tails that file. Release builds compile all of this to nothing.
// WP-03 has app_start, screen and idle; WP-03b adds drill (and runs the crash drill the test config names). The full set
// (focus, image, reader, mem, error, ...) comes in WP-08; WP-05 adds net. Main thread only (watchNet hands events over with
// brls::sync). Files go through SafeFs (S4).
namespace snx::app::testhooks {

// Empties the events file, starts the clock that every event's "t" (ms) counts from, and reads test/config.json.
// Call once, first. `fs` must outlive the test hooks (main() owns it).
void start(const platform::SafeFs& fs);

// {"ev":"app_start", version, build, platform, mode, profile, memTotal, memUsed, safeMode, lastCrash}
void appStart(const core::RuntimeInfo& runtime, std::string_view profile, bool safeMode, std::string_view lastCrash);

// The test config's server URL ("server": {"url": ...}), or "" (also in release builds).
std::string serverUrl();

// Writes a {"ev":"net",...} event for every request the client finishes (WP-05). The observer runs on the network thread and
// hands the event to the main thread with brls::sync. Release builds: nothing.
void watchNet(net::HttpClient& http);

// Writes {"ev":"image",...} for every finished image load and {"ev":"mem_pressure",...} when the memory guard acts (WP-07).
// Same hand-over as watchNet. Release builds: nothing.
void watchImages(image::ImageLoader& loader);

// The test config's "imageBench" (WP-07): paths below the server URL, and how often to load each. Empty / 0 without one.
std::vector<std::string> imageBenchPaths();
int imageBenchRepeat();

// Any other event: {"ev":<ev>, fields...}. Main thread only. Release builds: nothing.
void write(std::string_view ev, const nlohmann::ordered_json& fields);

// {"ev":"screen","name":...}: a screen is now on top. It also makes the next quiet spell report "idle" again.
void screen(std::string_view name);

// Once per main-loop iteration: writes "idle" when no animation has run for a few frames, starts the test config's crash
// drill the first time the app is idle, and retries lines that couldn't be written yet (on the Switch the file is busy
// while the harness reads it).
void onFrame();

// At exit: stops a running drill, then writes what is still pending (tries for about 100 ms).
void finish();

}  // namespace snx::app::testhooks
