# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** WP-13b (webtoon reader) **in progress**, then plan WP-18 (order: ROADMAP.md, D47).
Done: WP-00a … 13, 13c and 22; **v0.2.0 released** (v0.1.0 before it). Nice to have: WP-08b, 17, 21, 26.
## Last session (2026-11-12, WP-13b build, part 1, Opus)
- **Done, green** (`tools/build.sh tests`, contract 29/29, 0 warnings): `core/StripMath` (layout + anchor/pin, width and
  boxes, tile window, prefetch window + ends, flick, held/stick scroll, tap zones, spot note, "by itself" rules), config keys
  `reader.stripWidth` / `stripOrientation`, profile `stripScreensBehind/Ahead`, `ProgressSync` spot, `Chapter::stripSpotNote`,
  `UpdateStripProgress` + `setProgress(ReadProgress)`, recorded answers refreshed (only `meta: []`). `ui/reader/ProgressSaver`
  (lifted from ReaderModel, both readers use it). `ui/reader/StripModel` (3 chapters, joins at the ends with the view still,
  cut of the farthest, reading chapter at the middle line, read at the last page's bottom, spot saves, failed page and list
  retry, width/screen changes). Tests: `test_core_strip.cpp`, `test_ui_strip.cpp` (12 cases).
  View shape (instead of a shared base class): `ui/reader/StripCanvas` draws the strip inside `ReaderView`
  (`setStrip()`: strip mode; drags/flicks/taps go to the canvas, portrait + overlay + buttons stay shared). Desktop builds;
  **not wired into ReaderActivity yet**, so nothing uses it.
- **Split decided:** zoom in the strip moves to **WP-13d** (add it to ROADMAP.md at the end); the middle tap opens the menu.
- **Next steps:** `ReaderActivity` hosts the strip (`model_` paged or `StripModel` + `StripCanvas`; by hand and by itself;
  strip controls; settings with "Vertical strip" + "Strip width"; i18n; ConfigStore setters); test events; e2e `flick()` +
  middle-code check + `reader-webtoon`; docs, D49, ROADMAP WP-13d. Console spike after the activity works (owner wakes it).
  Gamma Webtoon has no genres (e2e takes the shape rule), 3 pages per chapter.
- From WP-13c: `maxDecodeScale` 4 on Full (safe 2, D48). Not released (CHANGELOG "Unreleased"). Console: normal build, asleep.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~30 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~120 s, `resume-progress` ~50–80 s,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s).
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline (2026-10-30): `fatal_errors` empty, `fatal_reports` 3 files + `dumps/` (qlaunch fatals from the Album: WP-08's, D41, and
  two dated Oct 29, before WP-12), `crash_reports` 26 files + `dumps/`.. Clock is off.
- **App mode:** Spelunky 2 = first HOME tile (playing another game moves it: the owner opens and closes Spelunky 2 again); hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
