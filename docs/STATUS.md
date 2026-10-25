# Status (handoff note, overwrite each session, ≤ 40 lines)

**Current phase:** 0 – Foundations
**Next WP:** WP-04 (core models + GraphQL, needs only WP-02, no console). WP-03c needs the console and the owner. Done: WP-00a … WP-03b.

## Last session (2026-10-25, WP-03b, Opus, owner away: PC side only) — done
- **Built** (design D33, `src/app/crash/README.md`): crash containment (libnx exception handler on a 32 KB stack, `diagAbortWithResult`,
  terminate handler, re-entrancy guard, pre-opened crash file, 64-line log ring, crash screen, safe mode next launch, watchdog),
  `platform::SafeFs` + `core::jailPath` (every write in the app now goes through it, test events and `app.log` included),
  `tools/safety/check-writes.sh` (grep gate), `check-elf.sh` now **enforcing** inside the Switch build (no `.nro` unless it passes),
  D28 stand-ins via `--wrap` (`tools/safety/stand-ins.txt`), `gate-selftest.sh`, `tools/symbolize-crash`, 10 crash drills.
- **Green:** unit tests (both builds), desktop E2E 14/14 (+`oom` skipped: Switch only), npm test 99, typecheck, Switch release and
  test builds (gate PASS: 0 banned, hooks ours 2/2, both stand-ins in use), gate self-test (S2 + S3 probes fail, no `.nro`).
- **For WP-08:** `makeRuntimeProfile()` must take `LastRun.crashed` (safe mode: budgets halved, prefetch off, no screen restore).
  New threads: `crash::prepareThread()`, explicit stack size, `addExitStep()` for pools, `recordWorkerException()` at task boundaries.
## Waiting for the console: WP-03c (`docs/wp/WP-03c-crash-drills-on-the-console.md`)
- `tools/e2e.sh switch crash-drills` twice in a row (never run on the device; the SwitchDriver's `requestExit/waitForExit/listFiles`
  are untested there). Check hbmenu (not HOME) after main-thread drills and no new `crash_reports` file (stray-thread risk, D33).
- Device record: build id = archive folder, symbolizes. `oom` drill. Switch golden of the crash screen. Watchdog pauses on HOME.
- Optional control case (handler off) only with the owner's yes and the owner watching.
## Open questions for the owner
- **D17/D28:** ban `appletSetCpuBoostMode` too? libnx's exit code (`_appletCleanup`) references it (only to undo a CPU boost nobody
  sets), so the ban needs a third do-nothing stand-in like D28's two. Recommended: yes. Until then the rule is commented out.
- Optional control case for the crash drills (SAFETY.md): yes or no, and when you can watch.
- A DHCP reservation for the PC (like the Switch has) would keep the test-server URL stable for device tests (WP-05). Owner's choice.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: write a script file and run `bash -l <file>`). Git Bash has no python.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 2.7 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` only `dumps/`, `crash_reports` 17 files + 10 in `dumps/`. Clock is off: compare lists.
- **App mode:** Spelunky 2 = first HOME tile; hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections for ~10 s after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing
  can't be read by another program (events use a pending queue; `app.log` readable after quit).
## Escalation
(none)
