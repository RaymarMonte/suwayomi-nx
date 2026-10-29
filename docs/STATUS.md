# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 2 – App (WP-08 …). Next:** WP-12 (MVP hardening, soak test, v0.1; needs the owner: "Try it for real").
WP-08b is nice to have for now (owner, 2026-10-29). Done: WP-00a … 11.

## Last session (2026-10-30, WP-11 console run, Opus) — WP-11 done
- PC side was done in `7941da3` (paged reader, `ReaderModel`/`ReaderView`/`ReaderActivity`, `ReaderMath`, `ProgressSync`, D44).
- **Console, all green:** `smoke`, `manga-details` (regression: the reader replaced the stand-in), then `reader-paged`,
  `reader-rtl-spread`, `resume-progress`, `broken-images` once with `--update-goldens` and once against the goldens.
  New Switch goldens (looked at, same as desktop): reader-paged page/menu/end, reader-rtl-spread spread, broken-images broken.
- **Acceptance numbers (application mode, stock clocks):** 100 prefetched turns median 18 ms, p90 19 ms, max 19 ms
  (limit 150 ms: the next frame at 60 Hz); memory 136.3 MiB after turn 20, growth 0.0 MiB (limit 16 MiB). Added to D44.
- No new crash report or fatal file. Test server stopped at the end.
- Follow-ups (D44): no download % under the spinner; the stick doesn't pan; a config.json save blocks a frame.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 4.2 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
  It sometimes won't start ("null cannot be cast"): run it again (`reset` first after a hard stop).
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~30 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~120 s, `resume-progress` ~50–80 s,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s).
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` 1 file + `dumps/` (WP-08's qlaunch fatal, D41), `crash_reports` 22 files + `dumps/` (4 from WP-03c, 1 from WP-08). Clock is off.
- **App mode:** Spelunky 2 = first HOME tile (playing another game moves it: the owner opens and closes Spelunky 2 again); hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
