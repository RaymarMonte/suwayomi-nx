# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 2 – App (WP-08 …). Now:** WP-08 (app shell, settings, logging, setup/login), **paused for the console**. Done: WP-00a … 07.

## Paused (2026-10-26, WP-08, Opus): the Switch runs are left (owner's rule: pause and sleep the PC when the console is needed)
- **Done and committed (D40, `src/ui/README.md`, `src/app/README.md`):** `AppContext`; Activity + Model screens (`snx_ui_models`,
  suite `ui`); Main (5 tabs, connection check = aboutServer + categories with a login), Setup (Tailscale/http/IP warnings),
  Login, Settings (server, log out, About: mode, memory, safe mode, crash logs); logger (`logs/app.log`, 2 s buffer, rotation);
  ConfigStore (debounced, `.bak`); applet-mode guard (libnx console); `net::inFlight()` in the idle rule; WP-08 test events.
- **Green:** unit 7/7 (+ contract), test-hooks, TSan, coverage 93.7 %, integration ×3, desktop build + `desktop-smoke.sh` (now the
  setup screen), all desktop e2e (smoke, setup-first-run, auth-matrix, applet-guard, low-memory, net-hello, image-bench, crash
  drills, harness), npm test + typecheck, Switch release + test build (gate PASS, `.nro` 4.2 MB).
- **Next (console on, HOME menu):** `tools/e2e.sh switch smoke --update-goldens` (no Switch golden yet: look at it), then
  `setup-first-run`, `auth-matrix`, `net-hello`, `image-bench`, `crash-drills`. Then `applet-guard` on the console: set
  `APPLET_MODE_READY = true` in SwitchDriver, drop its `drivers: ['desktop']`, **owner watching** the drill (S12). Then tick the
  WP checklist, STATUS, commit `WP-08: …`, push, CI.
- **Split off:** WP-08b (ROADMAP): all controllers merged (borealis stops at the first empty pad slot; needs a 2nd controller).
- **Notes:** the desktop text dialog ignores Enter (IME on): tests tap its OK hint. Old Hello screen = test-only Diagnostics.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 2.7 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `net-hello` ~35 s, `smoke` ~25 s, `crash-drills` ~9 min (one drill ~50 s). A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` only `dumps/`, `crash_reports` 21 files + `dumps/` (4 from WP-03c). Clock is off.
- **App mode:** Spelunky 2 = first HOME tile; hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
