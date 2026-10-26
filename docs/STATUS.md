# Status (handoff note, overwrite each session, ≤ 40 lines)

**Current phase:** 0 – Foundations
**Next WP:** WP-04 (core models + GraphQL, needs only WP-02, no console). Done: WP-00a … WP-03c.

## Last session (2026-10-26, WP-03c, Opus, owner present) — done
- **The console found 4 bugs, all fixed:** (1) every start crashed: libnx's `__start__` is a fixed 0 (now `_start` + a kernel
  query for the size). (2) `exit()` after a crash let hbmenu crash under creport (graphics left set up; a parked thread's
  stack still mapped out of the heap) → **D35** (owner OK): on the Switch every crash ends with `svcExitProcess`, after the
  crash screen if one shows; HOME then says "The software was closed because an error occurred." (no report, no fatal).
  (3) libnx's "MisalignedSP" (0x103) is any unaligned data access: record text fixed. (4) Driver: knows that note (crop
  `software-closed`), dismisses it after a screen check, compares the crash folders once more at scenario end.
- **D34** (owner OK): `appletSetCpuBoostMode` banned with `only-from=_appletCleanup` (a D28 stand-in can't reach a call inside
  libnx's own object file). `gate-selftest.sh` has 4 probes (call + stored pointer for the new rule).
- **Green:** `crash-drills` on the console twice in a row (10/10 each, 0 new fatal or crash-report files), Switch `smoke`,
  desktop E2E 14/14, unit tests, npm test 99, typecheck, release + test Switch builds (gate PASS), gate self-test 4/4.
  Records: `build_id` = archive folder, `symbolize-crash` → `Drills.cpp:39`. Watchdog 10.2 s; 22 s on HOME: no watchdog.
- Control case (owner watched): handler-less one-off build (the gate refused it; packed by hand, deleted after) → creport
  report for module `suwayomi-nx`, no record of ours, no fatal, back on HOME.
- **Known harness issue:** the console sometimes resets new connections for 30 s+ around app start/exit. Repeatable requests
  now retry ~60 s; a button press in a spell still fails the scenario (presses never repeat). Once in smoke, rerun passed.
  Idea for later: sys-autopilot-nx keeps its own network request.
## Open questions for the owner
- A DHCP reservation for the PC (like the Switch has) would keep the test-server URL stable for device tests (WP-05). Owner's choice.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 2.7 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Device: `crash-drills` ~9 min, one drill ~50 s. A start-up crash leaves no log: use temporary breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` only `dumps/`, `crash_reports` 21 files + `dumps/` (4 from WP-03c). Clock is off.
- **App mode:** Spelunky 2 = first HOME tile; hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
## Escalation
(none)
