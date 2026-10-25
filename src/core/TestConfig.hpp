#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Test config (docs/TESTING.md "Test hooks"): <data>/test/config.json, written by the E2E harness before a launch. Test builds
// only. WP-03b reads the crash "drill" from it; the server, auth and animation settings come with WP-08.
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

struct TestConfig {
    Drill drill = Drill::None;
    std::vector<std::string> problems;  // what couldn't be understood, in plain words (logged, otherwise ignored)
};

// Never throws: bad JSON or unknown values end up in `problems`.
TestConfig parse(std::string_view json);

}  // namespace snx::core::testconfig
