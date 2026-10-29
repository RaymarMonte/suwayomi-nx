# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 2 – App (WP-08 …). Next:** WP-11's **console run** (owner + Switch), then WP-12. WP-08b is nice to have for now
(owner, 2026-10-29). Done: WP-00a … 10; WP-11 on the PC.

## Last session (2026-10-30, WP-11, Opus) — PC side done, console run left
- **Paged reader (D44):** core `ReaderMath` (turn by side/direction, tap zones, swipes, fits, decode boxes, page position,
  prefetch window) + `ProgressSync` (lastPageRead ≤ every 5 s, isRead at the last page at once, all on leave/HOME/close);
  `ReaderModel` (page list, window −1/+3 with cancel outside, next chapter's list + 2 pages fetched near the end, chapter
  changes, a progress "saver" whose saves are never cancelled); `ReaderView` (page tiles as textures, tap/drag/swipe) and
  `ReaderActivity` (full screen, menu on A / middle tap, settings on MINUS: direction, fit, background, page number;
  direction + fit per manga in config.json). `platform::keepScreenOn` (Switch: `appletSetMediaPlaybackState`). The details
  screen reloads its chapters once the reader's last save ended. Events `reader`, `progress`, reader `error` (TESTING.md).
- **Green:** unit 7/7 + contract 28/28, typecheck, desktop e2e 25/25 incl. new `reader-paged`, `reader-rtl-spread`,
  `resume-progress`, `broken-images` (goldens looked at), Switch build (gate PASS, 0 warnings). Desktop: 100 turns, median 2 ms.
- **Next (console, owner present):** test server reset + start; `tools/e2e.sh switch` `smoke`, `manga-details`, then each new
  reader scenario once with `--update-goldens` (look at the new Switch goldens) and once without. `reader-paged` prints the
  turn times (p90 must be < 150 ms) and memory growth. Then tick WP-11's last box, ROADMAP "done", commit.
- Follow-ups: no download % under the spinner; the stick doesn't pan; a config.json save blocks a frame (D44).
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
- Device: e2e `smoke` ~27 s, `net-hello` ~25 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s). The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
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
