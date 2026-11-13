# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-15b** next (Opus): the waiting-save queue + WP-15's leftovers (ROADMAP.md), then WP-24.
Done: WP-00a … 13, 13b, 13c, 13d, **15** (desktop), 22; **v0.2.0 released** (13…15 unreleased). Nice to have: WP-08b, 17,
21, 26, last offline 18 / 18b (D51). From WP-13d: the console's longest strip frame is 31–32 ms (limit 34), not looked into.
## Last session (2026-11-13, WP-15, Opus) – done on desktop, committed
- **Built:** Updates and History tabs (D52): one view `ui/updates/ChapterRowsTab` on `ChapterRowsModel`, the day on the first
  row of each day, Update library / Stop with polling (`LibraryUpdater`, `core::LibraryUpdateWatch`), 5 GraphQL documents,
  `platform::utcOffsetSeconds` (logged at start), Epsilon Weekly (manga 157) + `addNewChapter` / `removeNewChapter`.
- **Found:** right after `updateLibrary` the timestamp has moved but `jobsInfo` still shows the previous job (SUWAYOMI-API.md).
  The scenario found: History missed reads made from the Library (now the lists reload when a screen on top of the main
  screen closes; a reload waits for a read / unread change on its way); X on a started chapter made it unread (now read, D43).
- **Tests:** unit + contract green; integration green in 3 modes; desktop e2e: `updates-history` green (new, ~22 s, goldens
  `updates-empty`, `updates-new`, `history`); Reading has 3 titles now (counts fixed in 9 scenarios); `reader-zoom`'s
  "menu after 250 ms" check uses `performance.now()` (WSL's wall clock jumped back once: -207 ms).
- **Not done (→ WP-15b):** the queue; B cancels "Opening…"; Switch runs (`updates-history`, library-browse, manga-details,
  resume-progress, reader-paged, reader-webtoon, offline-errors) + console numbers. Owner, optional: try it on the real server.
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
