# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 2 – App (WP-08 …). Next WP:** WP-11 (Paged reader + progress sync). WP-08b is nice to have for now (owner,
2026-10-29). Done: WP-00a … 10.

## Last session (2026-10-29, WP-10, Opus) — done
- **Manga details (D43):** `MangaModel` (details + all chapter pages, sort, filter, `continueReading`, actions, reload,
  refresh from source), `MangaActivity` (details left, one-column `CoverGrid` right, "..." row menu, X/Y/MINUS), and a
  `ReaderActivity` stand-in: **WP-11 replaces it** and calls `MangaModel::reloadChapters()` when the reader closes. New:
  `CoverImage`, `fitLines()`, `CoverGrid::rebind()`, `LibraryModel::updateManga`. API: `updateChapters`, `refreshManga`,
  `Chapter::lastReadAt`, `RuntimeProfile::maxChapters`. The server stamps `lastReadAt` whenever a patch sends `lastPageRead`
  (also 0), never for `isRead` alone, so "Continue" ignores stamps on unread page-0 chapters.
- Alpha's fixture description is long now ("Show more"); answers re-recorded. Events `manga`, `chapter_action`,
  `chapter_list`, `busy`, screen `Reader`; any screen event now starts a new quiet spell (`idle` follows it) (TESTING.md).
- **Green:** unit 7/7 + contract 28/28, integration ×3 (ui_login needed a second server start: "null cannot be cast"),
  npm + typecheck, desktop e2e 21/21 (one rerun: Xvfb's `import` failed once), Switch build (gate PASS). **Console:**
  `manga-details` (new goldens, then green against them), `smoke`, `library-browse` (46.8 → 46.9 MiB).
- Follow-ups: sort/filter aren't saved in config.json; a long opened description can't be scrolled with the stick.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 4.2 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
  It sometimes won't start ("null cannot be cast"): run it again (`reset` first after a hard stop).
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~27 s, `net-hello` ~25 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s). The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` 1 file + `dumps/` (WP-08's qlaunch fatal, D41), `crash_reports` 22 files + `dumps/` (4 from WP-03c, 1 from WP-08). Clock is off.
- **App mode:** Spelunky 2 = first HOME tile (playing another game moves it: the owner opens and closes Spelunky 2 again); hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
