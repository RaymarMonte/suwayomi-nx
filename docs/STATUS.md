# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** WP-13d (strip zoom): desktop done, console runs + owner's try left; then WP-18 (ROADMAP, D47).
Done: WP-00a … 13, 13b, 13c and 22; **v0.2.0 released** (v0.1.0 before it). Nice to have: WP-08b, 17, 21, 26.
## Last session (2026-11-12, WP-13d, Opus)
- **Zoom in the strip (D50):** pinch, middle double tap (2 ×, again 1), ZR / ZL; zoomed, drags pan both ways, D-pad
  left / right glide sideways, the stick's x pans; Rotate / Strip width go back to 1. Once the view stands still (150 ms)
  the pages on screen nearest the middle (profile `stripZoomPages`: Full 2, else 1) decode sharper unless already sharp.
  Code: `StripMath` zoom math, `StripModel::setView` / `sharpenOnScreen`, `StripCanvas` rewritten (per-page textures, swap).
- **Rest on whole units (D50):** a flick, glide, pinch, zoom or drag ends on whole screen units. `webtoon-seam` had hung
  on where a touch-stopped flick ended (a fraction of a pixel): desktop `webtoon-seam` / `webtoon-band` refreshed, a
  second run matched. **The Switch's two goldens likely need the same refresh.**
- **Green:** `build.sh tests`, `npm test` / `typecheck` / `contract`, full `e2e.sh desktop`, `build.sh switch` (gate PASS).
  `reader-webtoon`'s new Switch steps (ZR / ZL, a zoomed D-pad glide sideways) haven't run yet.
- **Next:** console (wake it, HOME): `tools/e2e.sh switch reader-webtoon --update-goldens`, check the two pictures, rerun
  with `--no-build` to confirm, then `reader-zoom`; console to sleep. Owner's try (`build.sh switch` + `deploy` in
  `tools/spike/lib.sh`), verdict into D50, ROADMAP WP-13d done. CHANGELOG stays Unreleased (a version is the owner's call).
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
