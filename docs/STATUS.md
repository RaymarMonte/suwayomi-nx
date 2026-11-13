# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** WP-15 planned (`docs/wp/WP-15-updates-and-history.md`). **Next: do WP-15** in a fresh Opus
session, then plan WP-24 (order: D47, D51). Done: WP-00a … 13, 13b, 13c, 13d, 22; **v0.2.0 released**.
Nice to have: WP-08b, 17, 21, 26, last offline 18 / 18b (owner, 2026-11-13, D51).
## Last session (2026-11-13, WP-15 planning, Opus, docs only)
- **Owner's answers:** Updates lists new chapters only (found after the manga was in the library, last 3 months, like
  Mihon); a tap opens the reader (Updates: that chapter; History: Continue, D43), the cover the details; a save that
  fails as the reader closes is kept across a restart (`progress-queue.json`, S5); the server's newer change wins over
  it. The rest are planner's defaults in the doc; D52 gets written when WP-15 is done.
- **Test server (v2.4.2366), now in SUWAYOMI-API.md:** the Updates and History queries work (History needs an explicit
  `and`: fields next to `or` are OR'ed); `updateLibrary`'s own answer is the previous job's status, so "done" =
  `lastUpdateTimestamp` changed + not running; the server's default settings skip manga with unread chapters / not
  started / completed (a fresh seed: all 156 skipped); a removed chapter folder + `fetchMangaAndChapters` drops the chapter.
- **Seed change in WP-15:** a new Epsilon Weekly (Reading) with a held-back chapter 3, added after the first browse so
  every existing id stays (WP-23 must not change the seed meanwhile). Size M/L, split point: the queue → WP-15b.
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
