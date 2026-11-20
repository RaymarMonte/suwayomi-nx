# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-18c built** (the rest of WP-18, D59); its **Switch runs are open** (below).
**Next:** WP-18c's console runs → 18b → 21 → 16 → 25 → 19; nice to have: 08b, 26, 27, 28.
Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 23, 24, 22; v0.2.0 (13…24; 17, 18 and 18c unreleased).
## Last session (2026-11-20, WP-18c, Opus) – desktop done, console runs open
- Built: the "Same server?" question when a new server address is saved while chapters are downloaded (owner's words,
  Same server / Another server; "Another" is remembered, the first setup never asks); the tab's `keep_awake` event; rows
  in `switch_downloads`; the ui_login token-expiry download (integration); new e2e `switch-download-queue` (slowed queue,
  Remove / top / bottom, Pause / Resume and keep-awake, a failed page + Retry, quit mid-download, "Same server?" (desktop),
  `sdFreeBytes`, 301 queued + memory rounds on the Switch); `switch-downloads` gains the server stopped and Gamma's strip.
- The lost D-pad press (Settings): borealis's default list scrolling moves the focus only once the next row is fully
  shown (it scrolls while the button is held), so a short press can end first. Settings now scrolls centered; the e2e
  press-again stays. Also fixed: "Failed at page N" named whichever of two failing pages gave up first (now the first).
- The e2e empties the server's page cache itself: **no reset is needed after integration** any more. Test builds: the
  idle rule ignores background requests (`inFlightForeground`), text fields take the `text` command.
- Green: unit, contract, fuzz (5 targets), npm test + typecheck, integration (3 modes; the token test: 1 refresh, no new
  login), all desktop e2e, Switch build + gate PASS. Flaky, server-bound (as before): desktop `server-downloads`'s
  "resume ≤ 1 s" (1.5–1.9 s twice, then 0.5 s on a rerun).
## Open: Switch runs (console on, Spelunky 2 first on HOME, application mode)
- `tools/e2e.sh switch switch-download-queue --update-goldens` (new: goldens `switch-queue-paused`, `switch-failed`; check
  them against desktop's), then `switch-downloads` (new steps, no new golden) and `server-downloads` (known state changed).
  Step 4 ("Same server?") is desktop only. The memory rounds (301 rows, + < 16 MiB) run only on the Switch.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`: java, javac, jar, jwebserver); WSL has none.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- Device: e2e `smoke` ~1 min, `touch-pass` ~50 s, `manga-details` ~67 s, `reader-paged` ~2 min, `switch-downloads` ~2.5 min, `server-downloads` ~3.7 min,
  `reader-webtoon` ~6.5 min, `reader-zoom` ~7 min, `updates-history` ~4.7 min, `offline-errors` ~2.7 min, `browse` ~5 min, `crash-drills` ~9 min.
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
## Escalation
(none)
