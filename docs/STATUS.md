# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 2 – App (WP-08 …). Next:** WP-13 (webtoon / continuous reader + zoom & pan; its WP file is still a short
note in ROADMAP.md: plan it first). WP-08b is nice to have for now (owner, 2026-10-29). Done: WP-00a … 12, **v0.1.0 released**.

## Last session (2026-10-30, WP-12, Opus) — WP-12 done, v0.1.0 on GitHub Releases
- **Fixed (D45):** a stopped server times out (the firewall drops packets) instead of refusing: now "Can't reach the server"
  (net: a connect timeout = Network); a failed isRead save was resent every frame and saves never stopped while the server
  was away: saves back off 5 → … → 60 s; pages that failed offline reload when turned to. Page cache 512 → 256 MiB (soak).
- **Owner's first real use:** a watchdog crash 10 s into typing the address (the Switch keyboard blocks the main loop):
  fixed with `crash::BlockingWait`; Switch-only test `keyboard-wait` (failed before, passes now).
- **New:** scenarios `offline-errors`, `soak`, `touch-pass`, `keyboard-wait`; README user part, CHANGELOG, `tools/release.sh`.
- **Green:** unit, integration, desktop; console `smoke`, `touch-pass`, `offline-errors`, `soak` ×2 (p90 18 ms, peak ~310 MiB),
  `crash-drills` 10/10, `keyboard-wait`; owner: Album refusal, sleep/wake in the reader, setup + reading on the real server.
- Follow-ups: a save failing as the reader closes isn't retried; covers failed offline stay fallback art; first-run log
  says "config: the answer is not valid JSON" (no config.json yet).
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
