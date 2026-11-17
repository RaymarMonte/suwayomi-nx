# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-23 done** (test source, D55). Next: **plan WP-14** (browse; it tests against the test source).
Done: WP-00a … 13, 13b, 13c, 13d, **15, 15b, 23, 24**, 22; **v0.2.0 released** (13…24 unreleased). Nice to have: WP-08b,
17, 21, 26, last offline 18 / 18b (D51). From WP-13d: the console's longest strip frame is 31–32 ms (limit 34), not looked into.
## Last session (2026-11-17, WP-23, Opus) – done, committed and pushed
- The test server now also has **our own extension** (Java, `tools/test-source/`, a plain jar built at `reset` with the PC's
  JDK), a **store** and a **test site** served by `jwebserver` on `127.0.0.1:4569` (started/stopped with Java). The seed adds
  the store and installs **SNX Test v1** (en 60 manga, ja 8, every filter type, paging); SNX Extra (fr) and SNX Mature (NSFW)
  wait in the store, and the store can offer SNX Test v2. How to use it: TESTING.md "Test source", `tools/test-server/README.md`,
  helpers in `tests/e2e/lib/test-source.ts`. All of the WP (no WP-23b). No app code changed, no Switch runs.
- Server quirks found (SUWAYOMI-API.md, D55 "as built"): content warning is a number (2 = NSFW), install right after uninstall
  needs a store read first, OkHttp reuses old store files unless dated ahead. Ids after the seed's are never fixed: look them up.
- Green: live check `tools/test-server/test-source.test.ts` (11) in `none` and `ui_login` on a fresh reset, `npm test` (141),
  typecheck, `tools/build.sh tests`, integration (3 modes), all desktop e2e (24 files, 34 tests; oom drill + keyboard-wait skip on desktop as before, ~14 min).
  Start: Windows side 6.2 s (Java ready 4.5 s, the site ~1 s more), seed step 0.1 s; a Java change rebuilds the jars in ~2 s.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`: java, javac, jar, jwebserver); WSL has none.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~30 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~120 s, `resume-progress` ~50–80 s,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s),
  `reader-webtoon` ~7 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min, `updates-history` ~4.7 min, `library-browse` ~2.4 min, `offline-errors` ~2.7 min.
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
- Harness: the console resets single new connections and spells of them (10–30 s+); requests retry ~60 s, inputs only if the console never had them (D32).
## Escalation
(none)
