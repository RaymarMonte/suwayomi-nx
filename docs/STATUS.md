# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-21 Stage A built** (2026-11-20, `docs/wp/WP-21-applet-mode.md`, D62); **P1 waits for the owner**.
**Next:** **WP-21** P1 on the console (owner watching), then Stage B/C → 16 → 25 → 19; WP-18d (the split) after WP-18b;
nice to have: 08b, 26, 27, 28. Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18b, 18c, 23, 24, 22;
v0.2.0 (17, 18, 18c, 18b unreleased).
## Last session (2026-11-20, WP-21 Stage A, Opus; the owner away, so no console step)
- Built the applet-mode crash ending: `platform::setExitMode` + `exitProcessNow` (Switch, applet mode) sends
  `ILibraryAppletSelfAccessor` command 10, waits ≤ `crashExitWaitMs` (5 s, new profile value), then `svcExitProcess`.
  Application mode and desktop unchanged. Plus an ending note `logs/crash-ending.tmp` (each step, synced; the next start
  logs it): with only 3 tries, it shows how far a failed try got. Details: D62 "As built, Stage A", `src/app/crash/README.md`.
- libnx 4.12 read again (`applet.c`, `init.c`): it sends command 10 only after closing every other service, then sleeps
  forever. We send it with everything open: if P1 fails, that's the first suspect (the plan's variants).
- Green: unit tests (7/7 + contract), desktop `applet-guard` (now checks the note) and `crash-drills` (10 + oom skipped),
  Switch build and test build 0 warnings, gate PASS, release dry run PASS (it left a `-dirty` build in `release/v0.2.0/`;
  the published files are on GitHub).
- `applet-guard` now has P1: the drill on the console only with `SNX_APPLET_DRILL=1` as well as `SNX_APPLET_CONSOLE=1`.
- The console wasn't touched: it still has the release build of 6e0da16 and is asleep.
## P1: the exact next step (owner watching; max 3 tries in all, D62)
1. Owner: console on, HOME menu, Spelunky 2 the first tile, airplane mode off; no `tests/e2e/out/STOP-console-fatal.txt`.
2. `wsl.exe -d Ubuntu --exec bash -lc 'cd <repo> && SNX_APPLET_CONSOLE=1 SNX_APPLET_DRILL=1 tools/e2e.sh switch applet-guard'`
   (refusal screen + golden, then the drill: 15 s of screenshots `out/applet-guard/ending-*.jpg`, `ended.txt`,
   `crash-ending.txt`, then the Album again). Pass = HOME menu (a note allowed), no new fatal / crash report, record parses.
3. A fatal = the owner reboots (POWER, Hekate → Launch → CFW (EMUMMC)), reads the report, deletes the STOP file. Then read
   `logs/crash-ending.tmp` on the SD card (before the app starts again) and pick the next variant (WP file, "If P1 fails").
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`). Git Bash has no python. The Bash tool turns `\\` in a heredoc into `\`: use the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`); WSL has none.
- Builds: unit tests ~14 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start: `reset`, start again.
- Device: e2e `smoke` ~1 min, `reader-paged` ~2 min, `switch-downloads` ~3.5 min, `reader-webtoon` ~6.5 min, `reader-zoom`
  ~7 min, `browse` ~5 min, `crash-drills` ~9 min. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC**, RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. HOME on the HOME menu resumes the app.
## Escalation
(none)
