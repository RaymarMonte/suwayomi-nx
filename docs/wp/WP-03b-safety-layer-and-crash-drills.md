# WP-03b Safety layer + crash drills

Model: **Opus** · Size: M · Needs: WP-03 · Owner: console available; decide on the optional control case

## Goal
Before any real app code lands, make it impossible (or at least caught automatically) for the app to crash the console or touch anything
outside its own folder. A crash in our code must end as "app closed + crash log", never Atmosphère's reboot screen. Drills run in application mode, the only
mode the app supports (D21). The applet-mode refusal screen gets its own drill in WP-08 (SAFETY.md S12).

## Read first
`docs/SAFETY.md` (all), `docs/TESTING.md` (Drivers, Launch mode), `src/app/` test hooks from WP-03.

## Deliverables
- [ ] `src/app/crash/`: `__nx_exception_stack` (32 KB), `__libnx_exception_handler`, `diagAbortWithResult` override, `std::set_terminate`,
      re-entrancy guard, pre-opened crash-log fd, in-memory log ring buffer, crash record format (documented in the module README)
- [ ] borealis crash screen for non-GPU aborts. GPU-origin aborts (result module 359) exit straight away
- [ ] Safe-mode flag on next launch after a crash (memory budgets halved and prefetch off, clear caches, don't restore last screen)
- [ ] `tools/symbolize-crash`: turns `BASE + 0x…` offsets in a crash log into function/file:line with `aarch64-none-elf-addr2line` and the matching `.elf`
- [ ] `platform::SafeFs` (write jail) + unit tests (traversal, prefixes, case, `sdmc:/atmosphere` etc.) + atomic-write helper (`tmp → fsync → rename`, `.bak`)
- [ ] CI grep gate: no raw write/rename/remove/mkdir calls outside `SafeFs`
- [ ] `tools/safety/check-elf.sh`: banned-symbol + required-override check on the `.elf` (SAFETY S2/S3), wired into the Switch build and CI.
      Baseline run on the Hello app. Record borealis's legitimate references and set up `allowed-symbols.txt` (owner OK for each entry)
- [ ] Also ban clock-changing APIs in `check-elf.sh` (D17, stock clocks only): `clkrstSetClockRate`, `pcvSetClockRate`, `apmSetPerformanceConfiguration`, `appletSetCpuBoostMode`
- [ ] Harness: sys-autopilot endpoint allowlist (S10) + unit test. Fatal-errors gate on every device scenario (new file → hard fail + stop)
- [ ] Test-config "drill" command in test builds: null write, misaligned access, worker stack overflow, `std::terminate`,
      `diagAbortWithResult(MAKERESULT(359,…))`, 2 GB `malloc`, exit request during cache write
- [ ] Scenario `crash-drills.test.ts` (device + desktop where meaningful), application mode, with the pass criteria from SAFETY.md
- [ ] Optional control case (handler disabled, application mode: expect an Atmosphère crash report, no crash log of ours, no reboot screen),
      **only if the owner says yes**, with the owner watching
- [ ] Watchdog thread (main loop stall > 10 s → log + clean exit) with a drill

## Acceptance
`crash-drills` green in application mode, twice in a row. Zero new `atmosphere/fatal_errors` files.
`check-elf.sh` fails when a banned symbol is deliberately linked, and passes on the clean build.

## Sonnet-safe chunks
- SafeFs traversal test table (after Opus writes the guard and the first cases).
- `symbolize-crash` script.
