# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-21 planned** (2026-11-20, `docs/wp/WP-21-applet-mode.md`, D62); not started.
**Next:** **WP-21** (applet mode, Opus, max effort; size L, split point → WP-21b) → 16 → 25 → 19; WP-18d (the split) after
WP-18b; nice to have: 08b, 26, 27, 28. Done: WP-00a … 13, 13b, 13c, 13d, 14, 14b, 15, 15b, 17, 18, 18b, 18c,
23, 24, 22; v0.2.0 (17, 18, 18c, 18b unreleased).
## Last session (2026-11-20, WP-21 planning, Opus, with the owner's answers)
- Read libnx 4.12's `applet.c`, nx-hbmenu and nx-hbloader: an Album crash is a fatal because our ending (D35's
  `svcExitProcess`) skips the "I'm leaving" message (`ILibraryAppletSelfAccessor` command 10) that hbmenu's quit sends;
  libnx exposes the session, so the crash path can send it. Details in the WP file and RESEARCH.md.
- Owner's answers: not provable in 3 tries → keep refusing the Album (D21 stays); the tip at every Album start with
  "Don't show again"; applet-mode console runs unattended once the proof (P1, P2) passed; every Switch scenario in both modes.
- **WP-21's first step is Stage A** (the ending + P1 on the console, owner watching: a failed try is a fatal the owner
  reboots from: POWER, Hekate → Launch → CFW (EMUMMC)). It decides whether the rest happens.
- No code changed. The console still has the normal (release) build of 6e0da16 and is asleep.
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
