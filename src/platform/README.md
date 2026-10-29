# platform

**Purpose:** hide the differences between the Switch (libnx) and the PC: the app's data folder, launch mode and memory
(`runtimeInfo()`, D12/D21), network up/down, focus/suspend/sleep hooks, keep-awake (WP-11), battery (later), and `SafeFs` (ARCHITECTURE.md "platform").

**Public headers** (include as `"platform/<Name>.hpp"`):
- `Platform.hpp`:
  - `platformName()` ("switch" or "desktop"), `dataDir()` (D10: `sdmc:/switch/suwayomi-nx/` or `./data/`).
  - `runtimeInfo()` → `core::RuntimeInfo`: Switch = `appletGetAppletType()`, the heap libnx got, `mallinfo().uordblks` (D12 amendment);
    desktop = application mode (or applet with `--simulate-applet-mode`), the PC's RAM (or `--mem-limit`), glibc's `mallinfo2()` (`uordblks + hblkhd`: big mmap'd blocks count too, WP-07).
  - `setDesktopTestFlags(simulateApplet, memLimitBytes)`: the two desktop test flags, set once by `main()`. The Switch ignores them.
  - `installQuitSignalHandlers()` / `quitRequested()`: desktop SIGINT/SIGTERM become a normal quit (the E2E harness uses it). Switch: no-ops.
  - `exitProcessNow(code)`: ends the process at once, no more of our code runs (Switch `svcExitProcess()`, desktop `_exit()`).
    Only for the crash path (`src/app/crash/README.md`).
  - `networkConnected()` (WP-08): Switch: nifm reports an IP address; desktop: true. Words an offline error.
  - `setResumeHandler(fn)` (WP-08): Switch: runs after a wake from sleep (libnx OnResume hook, main thread); the app resets the HTTP connections. Desktop: never.
  - `keepScreenOn(on)` (WP-11): Switch: `appletSetMediaPlaybackState` (no dimming, no auto-sleep while the reader is open; ends with the app). Desktop: nothing.
  - `showLaunchRefused(text, timeoutMs, onShown)` (WP-08, D21, S12): the applet-mode guard's screen. Switch: libnx text console until a new button/tap, an exit request or the timeout. Desktop: prints the text.
- `SafeFs.hpp` (WP-03b, SAFETY.md S4/S5, D14): the write jail. **Every** file create, write, rename, delete and mkdir in the app
  goes through a `SafeFs` rooted at `dataDir()`; `tools/safety/check-writes.sh` fails the build on raw calls anywhere else in
  `src/`. Each path is checked by `core::jailPath()` first (relative to the root or absolute inside it; `..`, `//`, case, the
  `sdmc:` prefix, odd characters and `.nro` files handled there) and refused otherwise; refusals go to a logger set with
  `setRefusalLogger()`. Functions: `makeDirs`, `writeAtomic` (`.tmp` → fsync → rename, optional `.bak`), `append` (open →
  append → close), `removeFile`, `renameFile` (replaces the target, also on Horizon where rename can't), `removeTree` (never the
  root, doesn't follow links), `removeTempFiles`, `listDir`, `readFile` (size cap), `exists`, `openLog` (the logger's file),
  `startPieces` (a file written in pieces that only gets its name on `commit()`, for the disk cache), and `prepareCrashFile`
  (opened at start-up, written by the crash handler without allocating). One implementation for both platforms (POSIX calls).

**Implementations:** `switch/` (libnx allowed only here) and `desktop/` (plain C++/POSIX). CMake compiles exactly one of them.
Host unit tests use the desktop one. `SafeFs.cpp` is shared by both (and is the only file allowed to call raw write functions).

**Depends on:** `core`. On the Switch also libnx (`<switch.h>`, linked through the app).

**Threading:** functions are safe to call from any thread unless their comment says otherwise. `setDesktopTestFlags()` and
`installQuitSignalHandlers()` run once at start-up, before other threads exist.

**Tests:** `tests/unit/test_platform.cpp` and `test_platform_safefs.cpp` (doctest suite `platform`, SafeFs in temp folders), desktop
implementation only. The Switch side is covered by device runs (`app_start` reports mode and memory).

**Status:** WP-03 added `runtimeInfo()`, the desktop test flags and the quit signals. WP-03b added SafeFs and `exitProcessNow()`.
WP-08 added `networkConnected()`, `setResumeHandler()` and `showLaunchRefused()`. WP-11 added `keepScreenOn()`. Battery: later.
