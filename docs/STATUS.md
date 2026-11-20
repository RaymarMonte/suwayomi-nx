# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-18b desktop done; Switch runs next** (`docs/wp/WP-18b-read-away-from-home.md`, D61). Not done.
**Next:** WP-18b's Switch runs + the owner's away-from-home try (below) → 21 → 16 → 25 → 19; WP-18d (the split) after
WP-18b; nice to have: 08b, 26, 27, 28.
Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18c, 23, 24, 22; v0.2.0 (13…24; 17, 18, 18c, 18b unreleased).
## Last session (2026-11-20, WP-18b build, Opus) – desktop done, no Switch runs
- Built (D61 "As built"): `core/Offline` + the conflict rule, `ui/main/Connection` (start / 30 s timer / focus / wake /
  suspicion / network checks), `progress.json` + `refreshProgress`, held waiting saves, the offline Library ("On this
  Switch"), details and reader (their saves go to the queue, sent once the server is back).
- **Split → WP-18d** (ROADMAP): gone chapters (online merge, tag, local saves), the gap line, the offline text of Updates /
  History / Browse / Server downloads, the e2e `offline-sync` scenario.
- Nothing on the Switch and offline: today's errors + Retry; going offline keeps the server's Library on screen, and a
  manga not on the Switch opens the normal way (both found by `offline-errors`).
- Green on the PC: `build.sh tests` (7/7, contract, write gate), fuzz (4 × 60 s), `npm test`, typecheck, integration (3
  modes, + the new offline-sync test), desktop + Switch builds (gate PASS, 0 warnings), desktop e2e `offline-reading`,
  `offline-errors`, `smoke`, `switch-downloads`, `switch-download-queue`, `server-downloads`, `resume-progress`, `updates-history`,
  `reader-webtoon` (failed once at the band, `read: true`, then passed: a flake to watch). Test server left running (auth none).
- **Next step (Switch; the owner readies the console: on, Spelunky 2 first on HOME):** `tools/e2e.sh switch offline-reading`,
  then `smoke`, `switch-downloads`, `switch-download-queue`, `resume-progress`, `updates-history`, `offline-errors`,
  `reader-webtoon`; by hand: HOME and back while offline (`connection` by focus), sleep / wake offline, the WP's numbers.
- **Then the owner's try away from home** (or airplane mode): download a few chapters, start offline, read, come home,
  the server has the pages. Only then is WP-18b done (ROADMAP row, STATUS); don't start WP-21 before.
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
