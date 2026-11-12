# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** WP-13b (webtoon reader) **built and green; waits for the owner's hands-on test**, then WP-13d (zoom in the strip), WP-18 (ROADMAP.md, D47).
Done: WP-00a … 13, 13c and 22; **v0.2.0 released** (v0.1.0 before it). Nice to have: WP-08b, 17, 21, 26.
## Last session (2026-11-12, WP-13b build, part 3, Opus)
- **WP-13b green on desktop and console:** the strip (`StripMath`, `StripModel` + `StripCanvas` in `ReaderView`,
  `ProgressSaver`, `ReaderActivity` hosts both readers; strip zoom split to **WP-13d**), scenario `reader-webtoon`. D49's
  console numbers: open by shape 0.52 s, as a strip 58 ms, worst frame 20 ms, memory flat, `texUsed` ≤ 61.1 MiB.
- **Console fixes this part:** WebP decodes at full size within the 15 % slack (libwebp's scaler cost 454 ms per strip, now
  ~180); a held D-pad / L / R counts as moving (the `reader` event came in the gap between the glide and the held scroll);
  `ConfigStore::postponeSaveThisFrame()` (a save costs 20–33 ms on the SD card: it waits while the reader moves); the
  `gesture` frame times leave out the test hooks' own SD writes (~8 ms per event, up to ~70 ms; release builds don't write).
- **Harness:** `test/commands/*` uploads skip the hash check; `reader-zoom`'s `portrait-zoom3` is taken before the swipe.
- **Next:** the owner reads a real webtoon from their server in both orientations (stick, D-pad, a flick, taps, a chapter
  change, coming back to a chapter) and says whether the strip picks itself for the right manga → D49's verdict, the WP's
  last box, then WP-13b is done. WP-13, 13c and 13b are unreleased (CHANGELOG): a version is the owner's call.
  The console was left asleep (2026-11-12): wake it (HOME) before the next device run.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~30 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~120 s, `resume-progress` ~50–80 s,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s),
  `reader-webtoon` ~7 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min.
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
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
