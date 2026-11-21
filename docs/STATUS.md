# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-21 Stages A + B done, P1 passed** (2026-11-21, `docs/wp/WP-21-applet-mode.md`, D62).
**Next:** **WP-21** Stage C (P2 with the owner watching, the console checks, Lite numbers, Switch scenarios from the
Album), docs → 16 → 25 → 19; WP-18d after WP-18b; nice to have: 08b, 26, 27, 28. Done: WP-00a … 13, 13b, 13c, 13d, 14,
14b, 15, 15b, 17, 18, 18b, 18c, 23, 24, 22; v0.2.0 (17, 18, 18c, 18b unreleased). **Open issues:** none (D63).
## Last session (2026-11-21, WP-21 P1 + Stage B, Opus; the owner watched P1)
- **P1 passed** (the null-pointer drill from the Album, command 10): 2 runs, no fatal, no crash report, the HOME menu came
  back with no note, our record parsed. D62 "P1 passed". The console has P1's **test build** and is **asleep**.
- **Stage B done** (0584cf7, 8539d7b, 678b3e6 + this commit): the `lite` profile, the low-memory screen
  (`refuseLowMemory`), the exit-request hook (`platform::setLeaveOnExitRequest`), the Album tip (`general.appletTip`,
  Settings switch from the Album only, About's words), `tools/e2e.sh … --mode applet`, `applet-start` (replaces
  `applet-guard`), D62 "As built, Stage B".
- Scenarios that check a Full number read `s.mode` and check Lite's (D62 lists them; `readerSettled` in lib/reader.ts).
  Applet mode found one reader bug, fixed for both modes: a spread from the memory cache was never checked (blurry on
  Continue; `ReaderModel::startLoad`, unit test, CHANGELOG).
- Tests: unit green; Switch build + gate PASS; **desktop: all 30 scenarios green in both modes** (each on its own: 78
  passed, 4 Switch-only skips). One-off flakes seen and passed on rerun: reader-zoom's drag down lost once; server-downloads'
  slowest action 1.8 s (desktop bound 1 s); the test server not restarting mid offline-errors (the harness's reset → 404s).
- **Next (Stage C, owner + console):** first make the Switch-only zoom parts Lite-aware (`reader-zoom`'s controls zoom to
  4 x, `reader-spread-split`'s measurement); then **P2** `SNX_APPLET_CONSOLE=1 tools/e2e.sh switch crash-drills --mode
  applet` (owner watching, the test build goes on first), then the console checks (WP file step 6).
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
