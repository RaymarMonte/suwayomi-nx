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
- `TestEvents.hpp`: test event lines (`line()`, `appStart()` with `safeMode`/`lastCrash`, `screen()`, `idle()`, `drill()`) and
  `IdleTracker` (when to write `idle`). The app's `src/app/TestHooks` writes them (TESTING.md "Test hooks").
- `SafePath.hpp` (WP-03b): `jailPath(root, path)`, the write jail's path rules (SAFETY.md S4); `platform::SafeFs` asks it first.
- `CrashRecord.hpp` (WP-03b): the crash record's format (`formatCrashRecord`, never allocates), `parseCrashRecord`, file names,
  `FixedText` (text in a fixed buffer) and `LogRing` (the last 64 log lines). Used by `src/app/crash/`.
- `TestConfig.hpp` (WP-03b): reads `test/config.json` (never throws); so far only `drill` (the crash drills).

**Depends on:** the C++17 standard library and nlohmann/json 3.12.0 (`third_party/`, include `<nlohmann/json.hpp>`).

**Threading:** no threads of its own. Functions are pure or work on caller-owned data, so they can be called from any thread.

**Tests:** `tests/unit/test_core.cpp` and `test_core_safety.cpp` (doctest suite `core`). Coverage target ≥ 80% lines (TESTING.md).

**Status:** WP-03 added launch options, runtime info and test events; WP-03b the write-jail path rules, the crash record and the
test config. Models and GraphQL come in WP-04.
