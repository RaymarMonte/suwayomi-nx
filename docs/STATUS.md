# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 2 – App (WP-08 …). Next WP:** WP-09 (Library screen). WP-08b (all controllers merged) can go any time. Done: WP-00a … 08.

## Last session (2026-10-26/27, WP-08, Opus) — done
- **App shell (D40, `src/ui/README.md`, `src/app/README.md`):** `AppContext`; screens = Activity + Model (`snx_ui_models`,
  suite `ui`); Main (5 tabs, never closes; check = aboutServer + categories with a login: aboutServer is public in ui_login),
  Setup, Login, Settings (About, crash logs); `StatusView`/`TextFieldCell`/`checkServer`; logger, ConfigStore, applet guard.
- **For WP-09+:** copy the screen pattern; a tab's view comes from `MainActivity::makeTab`; `checkServer` for connection;
  idle counts `http.inFlight()` + `images.activeJobs()`; touch coordinates go in `tests/e2e/lib/screens.ts`.
- **Applet mode (D41, owner OK):** the refusal screen is verified on the console. A crash in applet mode is always a system
  fatal (qlaunch stops when the Album's process ends abnormally, `0x290`), so the drill there runs on desktop only; console
  applet runs are opt-in (`SNX_APPLET_CONSOLE=1`, owner watching). Safer crash ending: WP-21.
- **Green:** unit 7/7 + contract, test-hooks, TSan, coverage 93.7 %, integration ×3, desktop smoke, all desktop e2e, npm,
  Switch build (gate PASS, `.nro` 4.2 MB). **Console:** smoke (new golden), setup-first-run, auth-matrix, net-hello,
  image-bench, crash-drills 10/10, applet-guard refusal screen.
- **Test server:** after its hard stop it sometimes won't start ("null cannot be cast to ... ServerConfig"): `reset` fixes
  it; auth-matrix does that by itself now.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 4.2 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~27 s, `net-hello` ~25 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s). The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` 1 file + `dumps/` (WP-08's qlaunch fatal, D41), `crash_reports` 22 files + `dumps/` (4 from WP-03c, 1 from WP-08). Clock is off.
- **App mode:** Spelunky 2 = first HOME tile; hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
