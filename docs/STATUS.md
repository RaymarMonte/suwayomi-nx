# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-21 done** (2026-11-22, `docs/wp/WP-21-applet-mode.md`, D62): the app opens from the Album.
**Next:** WP-16 (extensions; no WP file yet, so a planning session with the owner's answers first) → 25 → 19; WP-18d after
WP-18b; nice to have: 08b, 26, 27, 28. Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18b, 18c, 21, 22, 23, 24;
v0.2.0 (17, 18, 18c, 18b, 21 unreleased). **Open issues** (D63): none. IS-007 done 2026-11-24 (server-downloads
flake: the server holds a move to the top, test-only). Done 2026-11-24, owner's console tries passed: IS-004 (screen
changes cross-fade, D64), IS-006 (manga bar keeps its size while loading, D65), IS-008 (leaving the reader: the page
fades with the screen, D66). IS-005 done 2026-11-23 (desktop e2e flakes, test-only).
## Last session (2026-11-22, WP-21's last runs, Opus; the owner readied the console)
- **The whole Switch matrix in application mode: green in one run** (75 min, 28 scenario files / 37 tests; the 4 skipped
  are desktop-only: `low-memory`, two `harness` checks, the cache-write-killed drill). No fix needed. D62 has the line.
- **The owner's try passed** (before this session, the normal build, the owner's config): opened from the Album, read a
  chapter on the owner's server, quit to hbmenu. **Docking from the Album not tried**: the owner says it isn't a priority,
  so it stays unchecked (D62; on the PC `reader-portrait` covers the docked rule with `--simulate-docked`).
- So every WP-21 run is done: Switch and desktop green in both modes, P1 / P2 passed, the console checks in D62.
- The console has the **normal build** of this code again (`tools/build.sh switch`, gate PASS, uploaded, SHA-256 matched)
  and is **asleep**.
- Tests this session: the Switch matrix above. Unit tests, desktop in both modes and the Switch matrix from the Album were
  green at 7472af1 (2026-11-21); only docs changed since.
- From WP-21 (2026-11-21), still true: from the Album the heap is 259.7 MiB, Lite image cache ≤ 48 MiB, `ALBUM_HEAP_MB` 260;
  a very tall strip page decodes scaled down from the Album (D62, CHANGELOG); D58 amended (Resume shows "Starting...").
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`). Git Bash has no python. The Bash tool turns `\\` in a heredoc into `\`: use the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`); WSL has none.
- Builds: unit tests ~14 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start: `reset`, start again.
- Device: e2e `smoke` ~1 min, `reader-paged` ~2 min, `switch-downloads` ~3.5 min, `reader-webtoon` ~7 min, `reader-zoom`
  ~6 min, `soak` ~6.5 min, `browse` ~5 min, `crash-drills` ~9 min; the whole Switch matrix ~75 min. A start-up crash leaves
  no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC**, RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. HOME on the HOME menu resumes
  the app; HOME from the Album ends it (an exit request). After a wake the lock screen takes the first presses.
## Escalation
(none)
