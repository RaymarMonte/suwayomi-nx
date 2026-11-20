# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-18b done** (2026-11-20, `docs/wp/WP-18b-read-away-from-home.md`, D61).
**Next:** **WP-21** (applet mode; no WP file yet, plan it first, D57) → 16 → 25 → 19; WP-18d (the split) after WP-18b; nice
to have: 08b, 26, 27, 28. Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18b, 18c, 23, 24, 22; v0.2.0 (17, 18,
18c, 18b unreleased).
## Last session (2026-11-20 evening, WP-18b Switch runs + the owner's try, Opus, with the owner)
- Switch green: `offline-reading` (+ a new no-network start step), `smoke`, `switch-downloads`, `switch-download-queue`,
  `resume-progress`, `updates-history`, `offline-errors`, `reader-webtoon` (no band flake). Desktop green again for the
  changed ones. Numbers + device steps (sleep / wake offline by the owner, HOME and back) in D61 "Console".
- Found on the console and fixed: (1) no network: the check ended before the downloads were read, so error + Retry showed
  instead of the shelf; (2) the wake hook never ran since WP-08 (`appletSetRestartMessageEnabled(true)` was missing);
  (3) the 500-entry queue write took 51 ms on the main thread → now on one pthread worker (`std::thread` crashed the app on
  the console: it threw; S8 says pthreads). Unit test for the worker path added.
- e2e fixes: `switch-downloads` read a stale "loaded" event after deleting leftovers; the Switch `updates-history` goldens
  lacked WP-18's "Switch downloads" tab (updated); `offline-reading` cleans its Switch downloads up and tolerates leftovers.
- The numbers came from a one-off script (not committed). Test server left running (auth none). Console put to sleep.
- **The owner's try passed** (D61 "Owner's try"): started offline, read two downloaded chapters, and back home both waiting
  saves went to the server by themselves. The console still runs this test build (SD `test/` folder: `commands`, `events.jsonl`).
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`: java, javac, jar, jwebserver); WSL has none.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- Device: e2e `smoke` ~1 min, `touch-pass` ~50 s, `manga-details` ~67 s, `reader-paged` ~2 min, `switch-downloads` ~3.5 min, `switch-download-queue` ~3.7 min, `server-downloads` ~3.8 min,
  `reader-webtoon` ~6.5 min, `reader-zoom` ~7 min, `updates-history` ~4.7 min, `offline-errors` ~2.7 min, `browse` ~5 min, `crash-drills` ~9 min.
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
## Escalation
(none)
