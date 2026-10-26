# app

**Purpose:** `main()` and the wiring (D40): crash containment first (`crash/`, its own README), then the logger, the applet-mode
guard, config, borealis, the HTTP client, the Suwayomi client, the image loader, one `AppContext`, the main screen, the main loop.

**Files:**
- `main.cpp`: the start-up order (SAFETY.md S1, S12) and the exit order (S5: config, image loader, network, test events, log, crash file).
- `AppContext.hpp`: what every screen gets (fs, runtime, profile, last run, config, http, client, images). Made once, outlives the screens.
- `Logger.hpp/.cpp`: every `brls::Logger` line goes to stdout, the crash ring (S1) and `logs/app.log` (buffered, at most every
  `logFlushMs`, open -> append -> close, rotates to `app.1.log` at `logFileBytes`). Messages start with a tag: `"setup: ..."`.
- `ConfigStore.hpp/.cpp`: `config.json` in memory; saves debounced (`core::SaveScheduler`) with `writeAtomic` + `.bak`, `flush()`
  on HOME/sleep/exit, falls back to the `.bak` when the file is unreadable. `appClockMs()`: the app's steady clock.
- `AppletGuard.hpp/.cpp`: D21's refusal in applet mode (log line, `launch_refused`, the text console screen, nothing written after).
- `TestHooks.hpp/.cpp`: test events and the test config (docs/TESTING.md). Release builds compile it to nothing.

**Threading:** main thread, except where a comment says otherwise (the logger's buffer takes lines from any thread).

**Tests:** the pure parts are in core (`LogBuffer`, `SaveScheduler`, `TestConfig`, `TestEvents`: `tests/unit/test_core_app.cpp`).
The rest by the E2E scenarios (`applet-guard`, `setup-first-run`, `auth-matrix`, `low-memory`, `crash-drills`).

**Status:** WP-01 … WP-08.
