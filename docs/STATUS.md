# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. WP-13 (zoom & pan) and WP-13c (split spreads) done** (owner tried both 2026-11-10). **Next: plan
WP-13b** (webtoon reader; ROADMAP.md "How the Phase 3 WPs get their full docs"). Order: ROADMAP.md (D47).
Done: WP-00a … 13, 13c and 22; **v0.2.0 released** (v0.1.0 before it). Nice to have: WP-08b, 17, 21, 26.
## Last session (2026-11-10, WP-13c, Opus, side session)
- Built (rules as built: D48 "Spreads as built"): `core::SpreadSplit` + `reader.splitSpreads` (portrait default / always /
  never), the spread math (`test_core_spread.cpp`), `ReaderModel` halves in `step()` (back lands on the last half, progress
  by page, a prefetched spread decoded again at the doubled box in place), `ReaderView` draws a half, the 7th reader setting
  (the Dropdown's height is set so it doesn't scroll), test config `"maxDecodeScale"`, scenario `reader-spread-split`
  (goldens desktop + switch), `expectPage(..., way)` turns a portrait screenshot upright.
- **`maxDecodeScale` is 4 now** (Full; safe mode 2): at 4 a split spread zoomed 4 × is sharp after 240–253 ms, upload
  8.5–10 ms, frames p90 17 / max 17, +11.4 MiB while zoomed; at 2 zoom 4 gave nothing sharper (D48 "Console (WP-13c)").
- Green: `build.sh tests` 7/7, `tools/e2e.sh desktop` (21 files; after the cap change `reader-spread-split` and
  `reader-zoom` again), Switch gate PASS; console: `reader-spread-split`, `reader-zoom`, `reader-paged`, `reader-portrait`.
- Known limit: a page much bigger than 2000 × 1500 uploads more in one frame when it turns sharp (one tile per frame is
  the fix if a real chapter shows a hitch). Owner's try: "Works well". Not released (a version is the owner's call;
  CHANGELOG has it under Unreleased). The console has a normal (non-test) build and sleeps: wake it first.
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
