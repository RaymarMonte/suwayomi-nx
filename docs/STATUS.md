# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-18b planned** (`docs/wp/WP-18b-read-away-from-home.md`, D61); nothing built yet.
**Next:** **build WP-18b** (Opus, size L, split → WP-18d) → 21 → 16 → 25 → 19; nice to have: 08b, 26, 27, 28.
Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18c, 23, 24, 22; v0.2.0 (13…24; 17, 18 and 18c unreleased).
## Last session (2026-11-20, WP-18b planning, Opus) – plan only, no code
- The owner answered 8 questions (all the planner's picks): the Library of downloads as one shelf; offline and back online
  by itself (a check every 30 s, at waking); a finished chapter stays read, else the later reading wins, for every waiting
  save (**amends D53**, owner OK); go on past a missing chapter and say so; delete after reading as online; no marks or
  bookmarks offline; gone chapters shown, tagged "Removed from server".
- Test server facts (in the WP doc, for SUWAYOMI-API.md): every answer has a `Date` header (the server's clock for the
  "later" rule); `lastReadAt` is seconds; `chapters(filter: {id: {in: [...]}})` answers 400 chapters in 26 ms and simply
  leaves out ids the server no longer has. One probe write (Alpha ch 2, page 1) was put back to page 0.
- Found in the code: a connect timeout (5 s) isn't retried, so an offline start waits ≤ 5 s; `progress-queue.json` is written
  on the main thread, so the plan measures a 500-entry write on the console; `offline-errors` must accept the new checks.
- No tests run (docs only). Test server left running (auth none), as it was.
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
