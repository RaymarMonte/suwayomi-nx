# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-18 done** (download to Switch, D59; its Switch runs green 2026-11-20).
**Next:** **WP-18c** (the rest of WP-18, ROADMAP) → 18b → 21 → 16 → 25 → 19; nice to have: 08b, 26, 27, 28.
Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 23, 24, 22; v0.2.0 (13…24; 17 and 18 unreleased).
## Last session (2026-11-19/20, WP-18, Opus) – done (built on the 19th, Switch runs on the 20th)
- Built all of D59 but the "Same server?" question and some e2e steps (→ WP-18c, D59 "as built"). Green: unit, contract,
  fuzz seeds, npm test + typecheck, integration (3 modes), all desktop e2e, Switch build + gate PASS, and on the console
  `switch-downloads`, `smoke`, `manga-details`, `touch-pass`, `browse`, `server-downloads`, `reader-paged`, `reader-webtoon`,
  `resume-progress`, `offline-errors`. Switch goldens re-made, each checked by a pixel diff (only the sidebar or the menu moved).
- D59 numbers on the console: a 10-page Local chapter downloads in 666 ms (15 pages/s); a downloaded chapter's first page
  is on screen 165 ms after the tap (limit 300); `freeBytes()` takes 0–1 ms (no caching needed); HOME pauses in the same
  frame the app loses focus, downloads go on ~150 ms after coming back. `switch-downloads` writes them to
  `tests/e2e/out/switch-downloads/numbers.json` and checks both limits. Covered by unit tests only: the queue.json write rate.
- Fixed this session (tests only): (1) on the console a D-pad press toward a row below the visible part of a list is
  sometimes lost (borealis: nothing moves, no event), so `pressUntilFocus` (server- and switch-downloads) presses again
  after 3 s. Users see one press that does nothing under "Delete Switch downloads after reading" (WP-18c looks at it).
  (2) The server keeps the pages it served: integration's Paper Garden download made server-downloads' slowed queue
  finish at once. Integration now downloads Comet Courier. **Reset the test server before Switch e2e if integration ran.**
- Flaky, server-bound (not WP-18): desktop `server-downloads` "resume ≤ 1 s" (the server's own start, 0.5–2 s); Switch
  `server-downloads` "Clear" of 100 rows once took 10 s (the server holds it during a source call, WP-17). Reruns passed.
  Harmless: after Resume while the queue waits on a full space limit, keep-awake turns on for ~80 ms.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`: java, javac, jar, jwebserver); WSL has none.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- Device: e2e `smoke` ~1 min, `touch-pass` ~50 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~2 min, `resume-progress` ~1 min,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s), `switch-downloads` ~2.5 min,
  `reader-webtoon` ~6.5 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min, `updates-history` ~4.7 min, `library-browse` ~2.2 min, `offline-errors` ~2.7 min, `server-downloads` ~3.7 min, `browse` ~5 min.
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
## Escalation
(none)
