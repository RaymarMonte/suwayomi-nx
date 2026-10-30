# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** WP-22 (portrait reader) done and tried by the owner. **Next: WP-13.**
WP-08b is nice to have for now (owner, 2026-10-29). Done: WP-00a … 12, **v0.1.0 released**.
## Last session (2026-10-30, WP-22, Opus) — portrait reader done, owner's verdict in (D46)
- **New:** reader orientation (Y, menu Rotate, settings → Orientation; `reader.orientation`/`lastPortrait`); portrait draws
  through one NanoVG transform, ReaderView takes every touch and runs `addButton` buttons by the mapped point; D-pad turned,
  L/R keep sides; docked = landscape (`handheldMode()`, `--simulate-docked`); `reader` event: `orientation`, `screenW/H`, `x/y`.
- **Console** (handheld, app mode): `reader-portrait` ×2 and `reader-paged` ×3 green (100 portrait turns p90 18 ms, growth 0).
- **Owner's try:** feels right, leans to turned right. Quick taps were sometimes lost in both orientations: borealis makes a
  6-unit slide a drag and drops the tap. Fix: a touch that slid ≤ 40 units and let go within 300 ms is a tap (`core::isTap`
  in `ReaderView::onPan`). `reader-paged` turns a page by a slid tap (failed on the old build). Green: unit, desktop E2E all,
  console `reader-paged` (p90 19 ms; the owner's config came back byte-identical). The build runs for the owner's quick-tap try.
- **Left (owner):** whether quick taps now feel right on the device; whether this ships as v0.2.0 (CHANGELOG [Unreleased]).
- **Found:** a config save holds the main loop briefly (`reader-portrait` waits for it). **Fixed (chore):** Switch runs keep the
  owner's `config.json` (copy + exact restore, README "Device safety"). Follow-ups: saves off the main thread?; portrait D-pad repeats; D45's.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 4.6 MB. 0 warnings in our code.
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
- Baseline (2026-10-30): `fatal_errors` empty, `fatal_reports` 3 files + `dumps/` (qlaunch fatals from the Album: WP-08's, D41, and
  two dated Oct 29, before WP-12), `crash_reports` 26 files + `dumps/`.. Clock is off.
- **App mode:** Spelunky 2 = first HOME tile (playing another game moves it: the owner opens and closes Spelunky 2 again); hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
