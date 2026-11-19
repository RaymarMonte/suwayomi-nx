# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-17 under way, paused** (download on server, D58, `docs/wp/WP-17-download-on-server.md`).
**Next: resume WP-17** (Opus, M) from "Paused" below. Order (D57): WP-17 → 18 → 18b → 21 → 16 → 25 → 19; nice to have: 08b, 26, 27, 28.
Done: WP-00a … 13, 13b, 13c, 13d, **14, 14b, 15, 15b, 23, 24**, 22; **v0.2.0** (13…24 unreleased). WP-13d: the console's
longest strip frame is 31–32 ms (limit 34).
## Paused (2026-11-19, WP-17, Opus) – usage limit reached (the owner had said to ignore the 92 % pause this turn)
- **Done, green** (unit + contract 56/56, desktop 0 warnings, integration none/basic/ui_login): everything in the app
  (core, 8 documents, client, `ui/downloads/ServerDownloads` model, the Server downloads tab = tab 4, Settings = 5, details
  screen entries / MINUS list / filter / tags, Settings switch, reader "delete after reading" on close, events), unit tests,
  the integration case (Paper Garden, `delayMs` 1500), e2e `TABS`. The test server is stopped (integration stops it).
- **Next step:** write `tests/e2e/lib/server-downloads.ts` + `tests/e2e/scenarios/server-downloads.test.ts` (WP doc's e2e
  item). Facts gathered: Paper Garden = en-14, 8 chapters, 3 pages each (shared set), type Manhwa (check the reader mode);
  reach it by Browse → SNX Test (EN) → Latest → cover 0 (browse.test.ts helpers); details MINUS menu has 5 entries when
  not in the library (Download on server = index 4), the chapter menu 4 actions + the server entry (index 4 of 5); the
  desktop can press A, B, X, D-pad (X = the tab's Pause/Resume/Retry failed), not MINUS/Y: reach the tab's Clear button and
  the clear dialog (Cancel first) with the D-pad; row menu = Move to top, Move to bottom, Remove, [Retry], Open the manga.
  Fix: the tab's report() comment says a no-change poll writes no event, but `polls` is in the key (every read writes one).
- Then: `test-server.sh reset` + `start`, `tools/e2e.sh desktop server-downloads --update-goldens` (look at the goldens),
  the whole desktop suite, `npm test` + `npm run typecheck`, `tools/build.sh switch` (gate), docs (SUWAYOMI-API, READMEs,
  TESTING, CHANGELOG, D58 as built incl. "Downloading · 14 in the queue" and the known gap: a queue started elsewhere and
  still STOPPED shows Paused, not polled), ROADMAP, WP checklist, STATUS; commit; then the Switch runs (ask the owner).
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
  `reader-webtoon` ~7 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min, `updates-history` ~4.7 min, `library-browse` ~2.4 min, `offline-errors` ~2.7 min.
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
## Escalation
(none)
