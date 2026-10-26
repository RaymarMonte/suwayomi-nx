#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/AppConfig.hpp"
#include "core/RuntimeInfo.hpp"
#include "core/RuntimeProfile.hpp"
#include "image/ImageLoader.hpp"
#include "net/HttpClient.hpp"
#include "platform/SafeFs.hpp"

// Test events (docs/TESTING.md "Test hooks inside the app"). Test builds (SNX_TEST_HOOKS) write one JSON line per event to
// <data>/test/events.jsonl, and the E2E harness tails that file. Release builds compile all of this to nothing.
// WP-03: app_start, screen, idle. WP-03b: drill. WP-05: net. WP-07: image, mem_pressure. WP-08: launch_refused, mem, focus,
// error, server_check, login, config_saved, the idle rule with pending jobs, and the test config's server / setup /
// diagnostics settings. Main thread only (watchers hand their events over with brls::sync). Files go through SafeFs (S4).
namespace snx::app::testhooks {

// Empties the events file, starts the clock that every event's "t" (ms) counts from, and reads test/config.json.
// Call once, first. `fs` must outlive the test hooks (main() owns it).
void start(const platform::SafeFs& fs);

// {"ev":"app_start", version, build, platform, mode, profile, memTotal, memUsed, safeMode, lastCrash, budgets}
void appStart(const core::RuntimeInfo& runtime, const core::RuntimeProfile& profile, bool safeMode,
              std::string_view lastCrash);

// The applet-mode guard (D21): {"ev":"launch_refused",...}. appletGuardDrill() is the test config's null-pointer drill to run
// inside the guard's screen (after writing its "drill" event), or nullptr. flushNow() makes sure what is waiting reaches the
// file before the screen shows (it tries for about 100 ms).
void launchRefused(const core::RuntimeInfo& runtime);
void (*appletGuardDrill())();
void flushNow();

// The test config's server (also in release builds: then empty / nothing / false / 0).
std::string serverUrl();
// The server settings the app starts with instead of config.json's, when the test config gives a URL.
std::optional<core::ServerSettings> testServer();
// Show the setup screen with testServer() filled in, instead of starting with it.
bool showSetup();
// Open the diagnostics screen over the main screen; how many requests it sends at once after its server check.
bool diagnostics();
int diagnosticsBurst();
// The test config's "imageBench" (WP-07): paths below the server URL, and how often to load each. Empty / 0 without one.
std::vector<std::string> imageBenchPaths();
int imageBenchRepeat();

// Writes a {"ev":"net",...} event for every request the client finishes (WP-05). The observer runs on the network thread and
// hands the event to the main thread with brls::sync. Release builds: nothing.
void watchNet(net::HttpClient& http);

// Writes {"ev":"image",...} for every finished image load and {"ev":"mem_pressure",...} when the memory guard acts (WP-07).
// Same hand-over as watchNet. Release builds: nothing.
void watchImages(image::ImageLoader& loader);

// The idle rule (WP-08): `busy` says whether net or decode work is still pending (http.inFlight(), images.activeJobs()).
// The app counts as idle only when it is false and no animation runs. Also writes {"ev":"focus","view":...} on every focus change.
void watchApp(std::function<bool()> busy);

// Any other event: {"ev":<ev>, fields...}. Main thread only. Release builds: nothing.
void write(std::string_view ev, const nlohmann::ordered_json& fields);

// {"ev":"screen","name":...(,"params":{...})}: a screen is now on top. It also makes the next quiet spell report "idle" again.
void screen(std::string_view name);
void screen(std::string_view name, const nlohmann::ordered_json& params);

// {"ev":"error","where":<screen>,"kind":<ApiErrorKind name>,"msg":...}: a screen shows an error to the user.
void error(std::string_view where, std::string_view kind, std::string_view message);

// Once per main-loop iteration: writes "idle" when nothing has run for a few frames, a "mem" event every 10 s, starts the
// test config's crash drill the first time the app is idle, and retries lines that couldn't be written yet (on the Switch
// the file is busy while the harness reads it).
void onFrame();

// At exit: stops a running drill, then writes what is still pending (tries for about 100 ms).
void finish();

}  // namespace snx::app::testhooks
