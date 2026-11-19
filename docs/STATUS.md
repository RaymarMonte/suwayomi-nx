# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-18 built** (download to Switch, D59), desktop layers green; **its Switch runs are open**.
**Next:** the console runs below (console on, Spelunky 2 first on HOME) → then WP-18 is done → **WP-18c** (the rest of
WP-18, ROADMAP) → 18b → 21 → 16 → 25 → 19; nice to have: 08b, 26, 27, 28. Side fix D60 (Wait for sources) is in.
Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 23, 24, 22; v0.2.0 (13…24; 17 and 18 unreleased).
## Last session (2026-11-19, WP-18, Opus) – built, Switch runs open (owner away: PC put to sleep at the end)
- Built: the store + queue (`ui/downloads/`), the reader reads the SD card first (`ui/reader/ChapterPages`), the details
  screen's entries, MINUS list, Show filter and tags, the Switch downloads tab (tab 5: Server downloads and Settings one
  down), Settings' space + delete after reading, the HOME pause, keep-awake on the tab (S9). Not built: the "Same
  server?" question → WP-18c, with the e2e steps left over (D59 "as built").
- Green: unit tests, contract, fuzz seeds, npm test + typecheck, integration (3 modes, a new real-server downloader test),
  all desktop e2e incl. the new `switch-downloads` (~41 s), `tools/build.sh switch` + gate PASS. Flaky, not WP-18's:
  `server-downloads`' "resume ≤ 1 s" check is the server's own start (0.5–2 s across 3 runs; it passed alone).
- e2e: `screens.ts` TABS has Switch downloads at index 4; menus got one more entry (chapter menu, Show, MINUS; a dropdown
  shows 6 at a time, so a 7th entry scrolls and the first 6 keep their places). Desktop goldens with the sidebar re-made
  (checked by script: only the sidebar moved). Switch goldens not re-made: they fail on the sidebar until one run with
  `--update-goldens` (then look at them). Desktop has no Y: the scenario taps the footer's Y hint (x 812).
- **Open Switch runs:** `switch-downloads` (new), `smoke`, `manga-details`, `touch-pass`, `browse`, `server-downloads`,
  `reader-paged`, `reader-webtoon`, `resume-progress`, `offline-errors`; D59's numbers (first page ≤ 300 ms, a 10-page
  Local chapter ≤ 20 s, HOME pause ≤ 1 s, `freeBytes()` time). Then mark WP-18 done in ROADMAP and STATUS.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`: java, javac, jar, jwebserver); WSL has none.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- Device: e2e `smoke` ~30 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~120 s, `resume-progress` ~50–80 s,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s),
  `reader-webtoon` ~7 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min, `updates-history` ~4.7 min, `library-browse` ~2.2 min, `offline-errors` ~2.7 min, `server-downloads` ~4.2 min, `browse` ~5 min.
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
## Escalation
(none)
