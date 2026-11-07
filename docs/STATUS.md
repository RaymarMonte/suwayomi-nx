# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now: WP-13 (zoom & pan)**: built, desktop and console green; **only the owner's try is left**. Spreads
split off to **WP-13c** (D48). Next: the owner's try, then WP-13c, then plan WP-13b. Order: ROADMAP.md (D47).
Done: WP-00a … 12 and 22; **v0.2.0 released** (v0.1.0 before it). Nice to have: WP-08b, 17, 21, 26.
## Last session (2026-11-07, WP-13, Opus)
- Built (rules as built: D48): zoom math (`test_core_zoom.cpp`), `reader.zoomOnTurn`, `maxZoom`, the orientation-free decode
  cap, `ReaderModel::setPageZoom`, `ReaderView` zoom/pan/pinch/double tap/glides, ZL/ZR/D-pad/sticks (one press, one page),
  the test-hook touch channel + `lib/touches.ts`, scenario `reader-zoom` (goldens desktop + switch), docs.
- Green: `build.sh tests` 7/7, `npm test`, typecheck, `tools/e2e.sh desktop` (all 20 files), Switch gate PASS; console:
  `reader-zoom` (frames p90 17 / max 17 ms, `sharpMs` p90 183 ms, 50 zoom cycles and 100 zoomed turns p90 18 ms, memory +0),
  `reader-paged`, `reader-portrait` (prefetched turns p90 32 ms, growth 0), `touch-pass`. `reader-paged` now waits for the
  launch's config save (the menu's 250 ms wait had put its Settings tap into the save's stall: 1 in 3 desktop runs failed).
- Found: the desktop mouse reaches views as touch phase NONE + a MouseState (we count it as finger 0); a pan starts after the
  first step. A desktop e2e run under heavy load can lose a key press. D-pad turns: p90 32 ms now vs ~19 before (read in
  draw, so some turns show a frame later; target < 150 ms, a small polish item for later).
- **Left:** the owner pinches and reads a real chapter zoomed in, both ways, and says if "Back to whole page" feels right →
  D48's verdict; then mark WP-13 done in ROADMAP. The console was put to sleep: wake it before the next device run.
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
