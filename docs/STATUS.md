# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-24 done** (desktop + Switch, D54). Next by the roadmap: WP-14 (browse), plan it first.
Done: WP-00a … 13, 13b, 13c, 13d, **15, 15b, 24**, 22; **v0.2.0 released** (13…24 unreleased). Nice to have: WP-08b,
17, 21, 26, last offline 18 / 18b (D51). From WP-13d: the console's longest strip frame is 31–32 ms (limit 34), not looked into.
## Last session (2026-11-17, harness chore "the touch resets", Opus) – done, committed
- **Cause:** the console's network stack sometimes turns a new connection to sys-autopilot away (reset 1–5 ms after
  connecting, before any answer; ~20 per `updates-history` run, plus spells of 10–30 s, once ending in "host unreachable"
  for a few seconds). Reads were tried again, inputs never (D32), so one unlucky touch or tap ended the run (WP-24's 4 runs).
- **Fix (`tests/e2e/drivers/autopilot.ts`; D32 amended, owner OK):** an input asks first (`Expect: 100-continue`) and sends
  its body only after sys-autopilot's `100 Continue`. Its handler plays nothing without the body, so a reset before that,
  or a connection that never opened (refused, unreachable), is sent again (~1 min of tries, like reads); once the body
  has left it is never repeated. After a whole failed minute the next requests try once. sys-autopilot itself unchanged.
- **Tests:** npm unit tests (new: a stand-in console that resets before / after the body, or refuses), typecheck, all desktop
  e2e. Switch: `updates-history` green twice in a row (run 1 stopped on "host unreachable" before that part of the fix; run 2
  got a launch tap through a ~30 s spell), `touch-pass` green. Why the console's stack resets: not looked into (sys-autopilot's
  log is off and outside the allowlist). WP-24's known gap is in its WP file. Console asleep: wake it first.
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
  `reader-webtoon` ~7 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min, `updates-history` ~4.7 min, `library-browse` ~2.4 min, `offline-errors` ~2.7 min.
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
- Harness: the console resets single new connections and spells of them (10–30 s+); requests retry ~60 s, inputs only if the console never had them (D32).
## Escalation
(none)
