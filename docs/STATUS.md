# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-14 done** (browse, D56); next **WP-14b** (Search all, Tick for new manga, Languages;
ROADMAP). Done: WP-00a … 13, 13b, 13c, 13d, **14, 15, 15b, 23, 24**, 22; **v0.2.0** (13…24 unreleased).
Nice to have: WP-08b, 17, 21, 26, last 18 / 18b (D51). WP-13d: the console's longest strip frame is 31–32 ms (limit 34).
## Last session (2026-11-17, WP-14, Opus)
- **Built:** the Browse tab (sources by language, Last used, an "18+" tag), a source's screen (Popular / Latest, search,
  every filter type, paging, the "In library" tag; it opens with the focus on the first cover), the preview loaded from the
  source, Add (the picker in choose mode, Don't ask again, the same-title box), Remove (details menu, Library and Browse Y
  menus, a confirm), Settings "Ask for categories when adding". D56 as built has the numbers.
- **Tests:** unit 7/7, contract 48/48 (+ 4 recorded answers), integration 3 modes, npm test, the test-source live check
  (none, ui_login), all 25 desktop e2e; Switch: browse (incl. the Y / L / R / Settings step), library-browse, manga-details,
  touch-pass, updates-history green; smoke and library-tools: see below.
- **Found and fixed:** Suwayomi's icon route finds a cached icon by name prefix, so SNX Test showed SNX Mature's icon: the
  test packages are now `snx.testextra` / `snx.testmature` (SUWAYOMI-API.md); the console's stale cached icon was deleted
  once. The Switch smoke golden still had the pre-WP-24 Library bar (re-recorded). library-tools' Switch step tapped the
  old 2-entry cover menu (now 3 entries).
- **WP-14b starts with:** the api's `setCategoryDefault` and SourcesModel's hidden languages are in; the screens are not.
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
