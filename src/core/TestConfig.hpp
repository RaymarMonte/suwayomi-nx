#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/AppConfig.hpp"

// Test config (docs/TESTING.md "Test hooks"): <data>/test/config.json, written by the E2E harness before a launch. Test builds
// only. WP-03b reads the crash "drill" from it; WP-08 the server and its login, the setup screen and the diagnostics screen.
namespace snx::core::testconfig {

// The crash drills (docs/SAFETY.md "Crash drills", src/app/crash/README.md has what each one should end in).
enum class Drill {
    None,
    NullWrite,            // main thread writes through a null pointer
    Misaligned,           // main thread: misaligned exclusive load (Switch); desktop raises SIGBUS (x86 has no alignment faults)
    WorkerNullWrite,      // a worker thread writes through a null pointer: the crash screen shows, the main thread is fine
    WorkerStackOverflow,  // a worker thread recurses until its stack runs out
    Terminate,            // main thread calls std::terminate()
    GpuAbort,             // diagAbortWithResult(MAKERESULT(359, ...)), like deko3d on a GPU error (desktop: the same path, simulated)
    Oom,                  // asks for 2 GB blocks until the allocator says no (std::bad_alloc reaches the main loop)
    MainLoopException,    // a C++ exception escapes one main-loop iteration
    Watchdog,             // the main loop stops for 15 s: the watchdog ends the app after 10 s
    CacheWriteExit,       // a cache file is being written in pieces when the app is asked to quit
};

// Every drill with its name in the test config, in a fixed order. None is not in the list.
const std::vector<std::pair<Drill, std::string_view>>& drills();
std::string_view drillName(Drill drill);
// Drill::None for an unknown name.
Drill drillFromName(std::string_view name);

inline constexpr std::size_t kMaxBenchPaths = 8;
inline constexpr int kMaxBenchRepeat = 20;
inline constexpr std::size_t kMaxCredentialLength = 256;
inline constexpr int kMaxBurst = 100;

struct TestConfig {
    Drill drill = Drill::None;
    // "server": {"url": "http://<ip>:4568", "auth": "none|basic|ui_login", "username": ..., "password": ...}: the Suwayomi test
    // server. The app starts with these settings instead of config.json's (WP-08), as if they had been saved. serverUrl is empty
    // when missing or not a plain http(s) URL (then `problems` says why). A trailing "/" is removed.
    std::string serverUrl;
    std::optional<AuthMode> serverAuth;  // nothing = not given (none)
    std::string serverUsername;          // at most kMaxCredentialLength each
    std::string serverPassword;
    // "showSetup": true: show the setup screen with the server settings above filled in, instead of starting with them. The
    // Switch can't type in a test, so its first-run scenario taps "Continue" on the filled-in screen. The login screen also
    // fills in serverPassword when it is given.
    bool showSetup = false;
    // "diagnostics": {"burst": 20}: the diagnostics screen (WP-05's server check and request burst, WP-07's image benchmark)
    // opens over the main screen. It checks the server, then sends `burst` (0..kMaxBurst) requests at once. "imageBench" opens it too.
    bool diagnostics = false;
    int diagnosticsBurst = 0;
    // "imageBench": {"paths": ["/api/v1/manga/1/chapter/1/page/0", ...], "repeat": 5}: WP-07's image benchmark. The Hello
    // screen loads each image (server URL + path) `repeat` times through the image pipeline, decoding it every time, and writes
    // "image" events plus one "image_bench" summary. At most kMaxBenchPaths paths, each starting with "/"; repeat 1..20.
    std::vector<std::string> benchPaths;
    int benchRepeat = 0;  // 0 = no benchmark
    // "diskCache": false: the image loader runs without its SD-card cache (WP-12), so every image the app hasn't kept in RAM
    // comes from the server. offline-errors needs that: pages cached by earlier runs would load with the server stopped.
    bool diskCache = true;
    std::vector<std::string> problems;  // what couldn't be understood, in plain words (logged, otherwise ignored)
};

// Never throws: bad JSON or unknown values end up in `problems`.
TestConfig parse(std::string_view json);

}  // namespace snx::core::testconfig
