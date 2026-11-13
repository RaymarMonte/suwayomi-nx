# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** WP-15 in progress (Opus, 2026-11-13). Split at its planned point: the waiting-save queue is
**WP-15b** (ROADMAP.md), next after WP-15, before WP-24. Done: WP-00a … 13, 13b, 13c, 13d, 22; **v0.2.0 released**.
Nice to have: WP-08b, 17, 21, 26, last offline 18 / 18b (owner, 2026-11-13, D51).
## Last session (2026-11-13, WP-15, Opus) – committed, green so far
- **Done:** core (`UpdatesHistory`: new rule, window, days, one row per manga, `LibraryUpdateWatch`), 5 GraphQL docs + api
  calls + recordings, integration tests (3 modes), `ChapterRowsModel` + `LibraryUpdater` (unit-tested), one shared view
  `ui/updates/ChapterRowsTab` for both tabs (day label on the first row of each day, no separate header rows),
  `MainActivity` wiring, `platform::utcOffsetSeconds`, Epsilon Weekly (a Sonnet subagent, merged; manga 157, ch 460–461).
- **Found:** right after `updateLibrary` the timestamp has moved but `jobsInfo` still shows the previous job for a moment:
  done also needs "seen running", new counts, or the same state for `libraryUpdatePollMs` (put in SUWAYOMI-API.md + D52).
- **Not done yet (WP-15):** never run on screen; `app_start` log of the offset; e2e scenario `updates-history` (without
  the queue steps) + existing desktop scenarios (Epsilon may change `library-browse` goldens); B doesn't cancel
  "Opening…" yet; Switch build + gate; console runs/numbers; docs (D52, ARCHITECTURE, TESTING 17, READMEs, CHANGELOG).
- From WP-13d: the console's longest strip frame is 31–32 ms (limit 34), not looked into; WP-13…13d unreleased.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~30 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~120 s, `resume-progress` ~50–80 s,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s),
  `reader-webtoon` ~7 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min.
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
