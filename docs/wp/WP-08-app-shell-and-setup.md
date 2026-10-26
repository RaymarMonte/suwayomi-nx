# WP-08 App shell, settings, logging, test hooks, server setup/login

Model: **Opus** · Size: L · Needs: WP-03b, WP-06 · Owner: present once for the device `applet-guard` drill

## Goal
The real app skeleton: activity/tab navigation, config persistence, logger, full test-hook event set, input from all controllers,
and the first-run flow (server URL → auth → test connection → main screen). This sets the screen pattern every later screen copies.

## Read first
`docs/ARCHITECTURE.md` (ui, platform, errors, launch modes), `docs/SAFETY.md` S12, D21, `docs/TESTING.md` (test hooks, launch mode), `src/api/README.md`, borealis demo source for activities/tabs/swkbd.

## Deliverables
- [x] `src/app/main.cpp` wiring (`AppContext` holding config, client, loaders). No globals scattered around
- [x] Logger → `<data>/logs/app.log` (rotating, size-capped, buffered, flushed at most every few seconds and on exit), levels, tag per module.
      It also feeds the crash handler's in-memory ring buffer (WP-03b)
- [x] Config saves via `SafeFs` atomic write + `.bak`, debounced (SAFETY.md S5). Exit/HOME/sleep handler flushes everything within 1.5 s
- [x] "Closed unexpectedly last time" notice + safe mode UI (crash flag from WP-03b). Settings → About → "Crash logs" lists the files
- [x] Full `TestHooks` event set + `idle` detection (pending net/decode jobs = 0 and no animations) + test `config.json` override
- [ ] ~~Input: all connected pads merged (fixes the sys-autopilot player-2 case)~~ → moved to **WP-08b** (D40: borealis stops at the first empty slot; needs a second controller to check)
- [x] **Touch first (D16)**: everything in the shell, setup and login screens works by touch alone (tap tabs and buttons, drag/flick lists, tap a text field opens swkbd). Targets ≥ 72 px. Scenario drives it with the mouse on desktop
- [x] `platform`: focus/suspend/resume hooks, network status, memory stats (`mem` events every 10 s in test builds)
- [x] `platform::runtimeInfo()`: Switch = `appletGetAppletType()` + heap size + `mallinfo()` (not `svcGetInfo` "used", D12 amendment). Desktop = application mode unless `--simulate-applet-mode`,
      plus `--mem-limit=<MB>`. `AppContext` builds the `RuntimeProfile` (`Full`) once at startup and hands it to net/image/reader.
      `app_start` event includes mode + budgets. Settings → About shows mode + memory
- [x] **Applet-mode guard** (D21, SAFETY.md S12, must be crash-safe): first thing in `main()` after the logger and crash-handler set-up, before
      borealis, network, images or config. If `runtimeInfo()` isn't application mode (`AppletType_Application` / `AppletType_SystemApplication`;
      unknown values count as applet mode): log one line, emit `launch_refused` (test builds), then `platform::showLaunchRefused(text)` with the
      D21 text. Switch: libnx text console, closes on any button, a tap, or after 60 s. Desktop: prints the text. Then `main()` returns normally
      (back to hbmenu). If any step fails, skip the screen and exit. Test builds can run the null-pointer drill inside the screen
- [ ] Scenario `applet-guard` (**desktop green**; the device run waits for the owner) (desktop `--simulate-applet-mode` + device via the Album, see TESTING.md): screen shown (golden on Switch),
      `launch_refused` only, back at hbmenu, no new crash report or fatal file. On the device also the null-pointer drill inside the screen,
      **owner watching the first time** (it's the one place a crash would still be a system fatal)
- [x] Scenario `low-memory` (desktop `--mem-limit`): `mem_pressure` without a crash
- [x] Screen pattern: `XxxActivity` (view) + `XxxModel` (state/logic, unit-tested) + XML layout. Documented in `src/ui/README.md`
- [x] `MainActivity` tab frame with placeholder tabs (Library, Updates, History, Browse, Settings)
- [x] Setup screen: URL (swkbd), auth mode picker, username/password, "Test connection" (shows server version or a clear error), http-with-auth warning
  - When the URL is a Tailscale address (a `*.ts.net` name or an IP in `100.64.0.0/10`), say the Switch has no Tailscale and suggest the
    server's home-network address. The owner's own server URL is one (D18 "Update"), so they'd hit this on first run
- [x] Login screen for `ui_login`. Tokens persisted, auto-refresh, logout in Settings
- [x] Error/empty/loading view components reused by all screens
- [x] Scenarios: `smoke` (extended), `setup-first-run` (desktop types the URL; Switch pre-seeded), `auth-matrix`
- [ ] Device E2E run of all of them in application mode, plus `applet-guard` via the Album

## Acceptance
Unit (models) + desktop E2E + device E2E green. Restarting the app skips setup and lands on the main screen.
Opened from the Album, the app shows the D21 message and goes back to hbmenu, twice in a row, with zero new `atmosphere/fatal_errors` files.

## Sonnet-safe chunks
- Error/empty/loading components *after* Opus has defined their interface.
- i18n string table entries for the screens.
