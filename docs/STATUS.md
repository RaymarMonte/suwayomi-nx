# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-21 built; two things open** (2026-11-21, `docs/wp/WP-21-applet-mode.md`, D62).
**Next:** WP-21's open runs: (1) the whole Switch matrix in application mode once (`tools/e2e.sh switch`, ~62 min, console
ready), then the normal build back on the console; (2) the owner's try (open from the Album, read a chapter on the owner's
server, quit to hbmenu, dock once). Then 16 → 25 → 19; WP-18d after WP-18b; nice to have: 08b, 26, 27, 28. Done: WP-00a …
13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18b, 18c, 23, 24, 22; v0.2.0 (17, 18, 18c, 18b, 21 unreleased). **Open issues:** none (D63).
## Last session (2026-11-21, WP-21 P1, Stage B, P2, Stage C, Opus; the owner watched P1, P2 and the console checks)
- **P1 and P2 passed**: every crash drill from the Album ends at the HOME menu, no fatal (D62 "P2 passed").
- **The console checks** (D62 "The console checks"): from the Album the heap is **259.7 MiB**, the same with Spelunky 2
  suspended behind it; the keyboard opens; HOME (the exit request) ends the app in 0.4 s with the page saved; sleep and wake
  work (OnResume comes for the library applet); the screen stays on in the reader. **Docking not checked** (no dock that day).
- **Lite numbers:** image cache ≤ **48 MiB** (was 64), `ALBUM_HEAP_MB` 260; `soak` from the Album peaks at 99 MiB, no pressure.
- Found, kept as is (D62, CHANGELOG): one decode's budget (26 MiB) is less than the fixture's 800 × 12,000 strip page, so
  from the Album it decodes scaled down (672 wide) and a full stick push through it catches pages still loading.
- Test fixes from the Album: `reader-zoom` / `reader-spread-split` controls stop at 2 x; `reader-webtoon` / `image-bench`
  check the decode budget (strip goldens `perMode`); `switch-downloads` step 7: HOME ends the app, the queue comes back.
  Stale Switch goldens from before WP-18 (`library-browse`, `library-tools`) updated; Switch `applet-start` goldens made.
- **D58 amended (owner):** the server starts downloads only on its next 1-second beat, so Resume can show "running" up to
  ~1.9 s after the tap (from the Album on the PC 4 of 5 runs did). Resume's answer is now "Starting..." (at once), running ≤ 2 s.
- The console has the **normal build** of this code (SHA-256 checked) and is **asleep**.
- Tests: unit green; Switch build + gate PASS, `release.sh` dry run PASS; **Switch: all 30 scenarios green from the
  Album**, the touched ones green in application mode; desktop: every scenario green from the Album, the touched ones
  in application mode (the rest green there since Stage B). Open: the whole Switch matrix in application mode.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`). Git Bash has no python. The Bash tool turns `\\` in a heredoc into `\`: use the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`); WSL has none.
- Builds: unit tests ~14 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start: `reset`, start again.
- Device: e2e `smoke` ~1 min, `reader-paged` ~2 min, `switch-downloads` ~3.5 min, `reader-webtoon` ~7 min, `reader-zoom`
  ~6 min, `soak` ~6.5 min, `browse` ~5 min, `crash-drills` ~9 min; the whole Switch matrix ~62 min. A start-up crash leaves
  no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC**, RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. HOME on the HOME menu resumes
  the app; HOME from the Album ends it (an exit request). After a wake the lock screen takes the first presses.
## Escalation
(none)
