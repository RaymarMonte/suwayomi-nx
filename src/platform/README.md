# platform

**Purpose:** hide the differences between the Switch (libnx) and the PC: the app's data folder, launch mode and memory
(`runtimeInfo()`, D12/D21), network up/down, focus/suspend/sleep hooks, keep-awake, battery, and later `SafeFs` (ARCHITECTURE.md "platform").

**Public headers** (include as `"platform/<Name>.hpp"`):
- `Platform.hpp`:
  - `platformName()` ("switch" or "desktop"), `dataDir()` (D10: `sdmc:/switch/suwayomi-nx/` or `./data/`).
  - `runtimeInfo()` → `core::RuntimeInfo`: Switch = `appletGetAppletType()`, the heap libnx got, `mallinfo().uordblks` (D12 amendment);
    desktop = application mode (or applet with `--simulate-applet-mode`), the PC's RAM (or `--mem-limit`), glibc's `mallinfo2()`.
  - `setDesktopTestFlags(simulateApplet, memLimitBytes)`: the two desktop test flags, set once by `main()`. The Switch ignores them.
  - `installQuitSignalHandlers()` / `quitRequested()`: desktop SIGINT/SIGTERM become a normal quit (the E2E harness uses it). Switch: no-ops.

**Implementations:** `switch/` (libnx allowed only here) and `desktop/` (plain C++/POSIX). CMake compiles exactly one of them.
Host unit tests use the desktop one.

**Depends on:** `core`. On the Switch also libnx (`<switch.h>`, linked through the app).

**Threading:** functions are safe to call from any thread unless their comment says otherwise. `setDesktopTestFlags()` and
`installQuitSignalHandlers()` run once at start-up, before other threads exist.

**Tests:** `tests/unit/test_platform.cpp` (doctest suite `platform`), desktop implementation only. The Switch side is covered by
device runs (`app_start` reports mode and memory).

**Status:** WP-03 added `runtimeInfo()`, the desktop test flags and the quit signals. SafeFs comes in WP-03b, the rest with WP-08.
