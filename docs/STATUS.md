# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-18 planned** (download to Switch, D59, `docs/wp/WP-18-download-to-switch.md`).
**Next: run WP-18** (Opus, L, max effort). Order (D57): 18 → 18b → 21 → 16 → 25 → 19; nice to have: 08b, 26, 27, 28.
Done: WP-00a … 13, 13b, 13c, 13d, **14, 14b, 15, 15b, 17, 23, 24**, 22; **v0.2.0** (13…24, 17 unreleased).
## Last session (2026-11-19, WP-18 planning, Opus) – plan committed, no code
- Wrote the WP-18 doc, D59, the ROADMAP row and stubs (WP-18b's too). Owner's answers: words that name the Switch
  ("Download to Switch", "Delete from Switch", tag "On Switch"), a **"Switch downloads" tab** before Server downloads
  (amends D58's "Downloads"; space line, queue, downloaded manga), "Space for downloads" (default 8 GB, 1 GB always free,
  pause when full, nothing deleted by itself), its own "Delete Switch downloads after reading", paused at HOME / sleep, kept
  awake while the tab downloads (**S9 widened, owner OK**: SAFETY.md is updated in the build), Updates rows not now.
- Checked on the test server (facts in the WP doc; they go into SUWAYOMI-API.md during the WP): chunked page answers, a
  Content-Type that changes once cached, at most 5 source calls at once, new ids for returning chapters, `chapterState`
  without a source call. The probes left the test server `reset` + running (no auth).
- From WP-17's run, still true: desktop X = the middle mouse button (the next key focuses what's under the pointer), so the
  desktop scenario taps and the Switch presses X; details row taps need a fresh screen (a sort / filter change scrolls).
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
