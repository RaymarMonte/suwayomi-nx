# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 2 – App (WP-08 …). Now:** WP-09 (Library screen): PC side done, the console run is left. WP-08b is nice to have for
now (owner, 2026-10-29). Done: WP-00a … 08.

## Last session (2026-10-29, WP-09, Opus): PC side done, waiting for the console
- **Library tab (D42; recipe in `src/ui/README.md` "How to make a list screen"):** `LibraryModel` (MainActivity owns it),
  `CoverGrid` + `GridLayout` (recycling grid), `LibraryTab` (category bar, Sort, Refresh, covers + unread badge + 2-line
  title), `MangaActivity` stand-in (WP-10). L/R, Y, X, MINUS menu (PLUS stays quit, owner OK), all by touch too. config.json
  `library {categoryId, sort}`; `AppContext::textures` (S7). Events `library`, `grid`, screen `Manga`.
- **Test server:** the seed adds the 150-manga "Big shelf" (`SHELF` in fixtures.ts) and an "Empty" category; 日本語 is now id 156.
  Fixtures re-recorded. Found by the scenario and fixed: a focused Box cell crashed borealis's focus search (null index).
- **Green:** unit 7/7 + contract, integration ×3, all 20 desktop e2e (incl. `library-browse`, new smoke golden), npm 106 +
  typecheck, Switch build (gate PASS). Desktop memory over 3 passes of the shelf: 76 → 78 → 76 MB.
- **Next (console, owner present):** console on, airplane mode off, Spelunky 2 first on HOME. `tools/test-server/test-server.sh
  reset` + `start`, then `tools/e2e.sh switch smoke --update-goldens` (Library tab changed) and `tools/e2e.sh switch
  library-browse --update-goldens` (its Switch-only part presses R/L/Y/MINUS); look at the new goldens; both again without the
  flag. Then tick WP-09's last box, ROADMAP "done", commit `WP-09: ...`, push.
- Screens = Activity + Model; idle counts `http.inFlight()` + `images.activeJobs()`; any crash in applet mode is a system
  fatal (D41). Test server won't start after a hard stop ("null cannot be cast"): `reset`.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 4.2 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~27 s, `net-hello` ~25 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s). The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` 1 file + `dumps/` (WP-08's qlaunch fatal, D41), `crash_reports` 22 files + `dumps/` (4 from WP-03c, 1 from WP-08). Clock is off.
- **App mode:** Spelunky 2 = first HOME tile; hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
