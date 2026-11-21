# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-21 Stage A done, P1 passed** (2026-11-21, `docs/wp/WP-21-applet-mode.md`, D62).
**Next:** **WP-21** Stage B (PC), then Stage C (P2 with the owner watching) → 16 → 25 → 19; WP-18d (the split) after WP-18b;
nice to have: 08b, 26, 27, 28. Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18b, 18c, 23, 24, 22;
v0.2.0 (17, 18, 18c, 18b unreleased). **Open issues:** none (`docs/ISSUES.md`, D63).
## Last session (2026-11-21, WP-21 P1, Opus; the owner watching)
- **P1 passed** (the null-pointer drill from the Album, Stage A's command-10 ending): 2 runs, no fatal, no new
  `fatal_*` / `crash_reports` file, the HOME menu came back with no note, our record parsed, the Album opened again.
  The ending note had only the "sending command 10" line: the system ends the process inside the call. D62 "P1 passed".
- Run 1's test failed only on its last step (UP + 12 × LEFT missed the first tile); `applet-guard`'s `backToTheFirstTile`
  now presses HOME; run 2 green end to end (`SNX_APPLET_CONSOLE=1 SNX_APPLET_DRILL=1 tools/e2e.sh switch applet-guard`).
- The console now has the **test build** of this commit (P1 put it there) and is **asleep**. A normal build goes back at
  the end of WP-21 (or when the owner asks).
## Stage B (PC only, no console): in progress
- Done (unit tests, desktop + Switch builds green, gate PASS): the `lite` profile (`tooLittleMemoryForLite`,
  `liteMinFreeBytes` 160 MiB), the guard = the low-memory screen (`refuseLowMemory`, `launch_refused` `reason`), the
  exit-request hook (`platform::setLeaveOnExitRequest`, and on the guard's screen), the Album tip (`general.appletTip`
  in config.json, `showAppletTip`, test config `appletTip`, `applet_tip` event, MainActivity before setup/login/the
  crash note, Settings → "Show the Album tip" from the Album only, About's words).
- Left: e2e `--mode applet` (both drivers, Switch "close running software" step), `applet-start` replacing
  `applet-guard` (desktop: low-memory screen + drill), applet goldens, every desktop scenario in both modes, D62 notes.
  Until then the desktop `applet-guard` scenario fails (applet mode no longer refuses). Then P2 (owner watching).
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
