# platform

**Purpose:** hide the differences between the Switch (libnx) and the PC: the app's data folder, launch mode and memory
(`runtimeInfo()`, D12/D21), network up/down, focus/suspend/sleep hooks, keep-awake, battery, and later `SafeFs` (ARCHITECTURE.md "platform").

**Public headers** (include as `"platform/<Name>.hpp"`):
- `Platform.hpp`: `platformName()` ("switch" or "desktop"), `dataDir()` (D10: `sdmc:/switch/suwayomi-nx/` or `./data/`).

**Implementations:** `switch/` (libnx allowed only here) and `desktop/` (plain C++/POSIX). CMake compiles exactly one of them.
Host unit tests use the desktop one.

**Depends on:** `core`. On the Switch also libnx (from WP-03b/WP-08 on).

**Threading:** functions are safe to call from any thread unless their comment says otherwise.

**Tests:** `tests/unit/test_platform.cpp` (doctest suite `platform`), desktop implementation only. The Switch side is covered by
device runs.

**Status:** placeholder from WP-01. `runtimeInfo()` and SafeFs come in WP-03b/WP-08.
