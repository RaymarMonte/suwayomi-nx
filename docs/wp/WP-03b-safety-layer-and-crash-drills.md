# WP-03b Safety layer + crash drills

Model: **Opus** · Size: M · Needs: WP-03 · Owner: console available; decide on the optional control case

## Goal
Before any real app code lands, make it impossible (or at least caught automatically) for the app to crash the console or touch anything
outside its own folder. A crash in our code must end as "app closed + crash log", never Atmosphère's reboot screen. Drills run in application mode, the only
mode the app supports (D21). The applet-mode refusal screen gets its own drill in WP-08 (SAFETY.md S12).

## Read first
`docs/SAFETY.md` (all), `docs/TESTING.md` (Drivers, Launch mode), `src/app/` test hooks from WP-03.

## Deliverables
Status 2026-10-25: everything that runs on the PC is done and green. The console runs moved to
[WP-03c](WP-03c-crash-drills-on-the-console.md) (the owner was away). Design: D33, `src/app/crash/README.md`.
- [x] `src/app/crash/`: `__nx_exception_stack` (32 KB), `__libnx_exception_handler`, `diagAbortWithResult` override, `std::set_terminate`,
      re-entrancy guard, pre-opened crash-log fd, in-memory log ring buffer, crash record format (documented in the module README)
- [x] borealis crash screen for non-GPU aborts. GPU-origin aborts (result module 359) exit straight away. (Our own `ui::CrashActivity`:
      borealis's `Application::crash()` is an empty stub. Shown when the main thread is healthy: worker crashes, exceptions out of a
      main-loop iteration. Main-thread hard crashes can't show it and get the "closed unexpectedly" note at the next launch.)
- [x] Safe-mode flag on next launch after a crash: `app_start.safeMode`, caches cleared, the note. Halved budgets and prefetch off
      come with WP-08's `makeRuntimeProfile()` (it takes the flag); there is no "restore last screen" yet to switch off.
- [x] `tools/symbolize-crash`: turns `BASE + 0x…` offsets in a crash log into function/file:line with `aarch64-none-elf-addr2line` and the matching `.elf`
      (also desktop records with the PC's addr2line). Checked on a desktop record and a hand-made Switch one; a real device record in WP-03c.
- [x] `platform::SafeFs` (write jail) + unit tests (traversal, prefixes, case, `sdmc:/atmosphere` etc.) + atomic-write helper (`tmp → fsync → rename`, `.bak`)
- [x] CI grep gate: no raw write/rename/remove/mkdir calls outside `SafeFs` (`tools/safety/check-writes.sh`, with a self-test)
- [x] `tools/safety/check-elf.sh`: banned-symbol + required-override check on the `.elf` (SAFETY S2/S3), wired into the Switch build and CI.
      Baseline run on the Hello app. Record borealis's legitimate references and set up `allowed-symbols.txt` (owner OK for each entry)
      (runs before `elf2nro`: no `.nro` unless it passes. D28 stand-ins via `tools/safety/stand-ins.txt`; `allowed-symbols.txt` is empty)
- [x] Also ban clock-changing APIs in `check-elf.sh` (D17, stock clocks only): `clkrstSetClockRate`, `pcvSetClockRate`, `apmSetPerformanceConfiguration`, `appletSetCpuBoostMode`
      (all but `appletSetCpuBoostMode`, which libnx's exit code references: its rule waits for the owner's OK on a D28 stand-in, WP-03c)
- [x] Harness: sys-autopilot endpoint allowlist (S10) + unit test. Fatal-errors gate on every device scenario (new file → hard fail + stop)
      (built in WP-03; the new `waitForExit()` runs the same crash-folder check)
- [x] Test-config "drill" command in test builds: null write, misaligned access, worker stack overflow, `std::terminate`,
      `diagAbortWithResult(MAKERESULT(359,…))`, 2 GB `malloc`, exit request during cache write (plus worker null write, an exception
      out of the main loop, the watchdog)
- [x] Scenario `crash-drills.test.ts` (device + desktop where meaningful), application mode, with the pass criteria from SAFETY.md
      (desktop green; the device runs are WP-03c)
- [ ] Optional control case (handler disabled, application mode: expect an Atmosphère crash report, no crash log of ours, no reboot screen),
      **only if the owner says yes**, with the owner watching → WP-03c
- [x] Watchdog thread (main loop stall > 10 s → log + clean exit) with a drill

## Acceptance
`crash-drills` green in application mode, twice in a row. Zero new `atmosphere/fatal_errors` or `atmosphere/fatal_reports` files.
→ desktop green (10 drills; `oom` is Switch only); the console part is WP-03c.
`check-elf.sh` fails when a banned symbol is deliberately linked, and passes on the clean build.
→ done: `tools/safety/gate-selftest.sh` (S2 `fatalThrow`, S3 `setsysSetColorSetId`: both builds fail, no `.nro`), clean builds pass.

## Sonnet-safe chunks
- SafeFs traversal test table (after Opus writes the guard and the first cases).
- `symbolize-crash` script.
