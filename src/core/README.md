# core

**Purpose:** everything that is pure C++: models, JSON mapping, GraphQL documents, reader math, cache and retry policies,
`RuntimeProfile`, config (ARCHITECTURE.md "core"). It must never include curl, borealis, libnx or platform headers, so it is
100% testable on the PC.

**Public headers** (include as `"core/<Name>.hpp"`):
- `Version.hpp`: `appVersion()` (from CMake), `buildFlavor()` ("test" with `SNX_TEST_HOOKS`, else "release"), `versionLabel()`.
- `LaunchOptions.hpp`: `parseLaunchOptions(argc, argv)` → `-d`, `--log-file`, `--simulate-applet-mode`, `--mem-limit=<MB>`; never throws,
  unknown flags and bad values go into `problems`.
- `RuntimeInfo.hpp`: `LaunchMode` (Application/Applet), `launchModeName()`, `RuntimeInfo {mode, totalMem, usedMem}` (filled by
  `platform::runtimeInfo()`, input for WP-08's `makeRuntimeProfile()`).
- `TestEvents.hpp`: test event lines (`line()`, `appStart()`, `screen()`, `idle()`) and `IdleTracker` (when to write `idle`). The app's
  `src/app/TestHooks` writes them (TESTING.md "Test hooks").

**Depends on:** the C++17 standard library and nlohmann/json 3.12.0 (`third_party/`, include `<nlohmann/json.hpp>`).

**Threading:** no threads of its own. Functions are pure or work on caller-owned data, so they can be called from any thread.

**Tests:** `tests/unit/test_core.cpp` (doctest suite `core`). Coverage target ≥ 80% lines (TESTING.md).

**Status:** WP-03 added launch options, runtime info and test events. Models and GraphQL come in WP-04.
