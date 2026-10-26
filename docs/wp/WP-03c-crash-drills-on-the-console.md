# WP-03c Crash drills on the console

Model: **Opus** · Size: S/M · Needs: WP-03b · Owner: console on and reachable (HOME menu's first tile = Spelunky 2); decide the
two open questions below; watch the console for the optional control case

Split from WP-03b (2026-10-25): WP-03b built everything and made it green on the PC, but ran nothing on the console (the owner was
away). This WP is the device half of WP-03b's acceptance.

## Goal
Prove on the real console that every crash drill ends as "app closed + crash record", never as Atmosphère's reboot screen, in
application mode, twice in a row. Until then the crash handler is untested on the hardware it exists for. WP-08's applet-mode
guard drill (S12) must not run before this is green: in applet mode a crash the handler misses is a system fatal.

## Read first
`docs/STATUS.md`, `src/app/crash/README.md` (what each drill should end in), `docs/SAFETY.md` "Crash drills",
`tests/e2e/README.md` "Device safety", D33 in `docs/DECISIONS.md`.

## Deliverables
- [x] `tools/e2e.sh switch crash-drills` green, **twice in a row**, application mode. Zero new files in `atmosphere/fatal_errors` or
      `atmosphere/fatal_reports`, and no new `atmosphere/crash_reports` file (that would mean creport saw the crash, not our handler).
      The Switch side of the scenario (`SwitchDriver.requestExit/waitForExit/listFiles`) was written in WP-03b but never run: fix what
      the first run shows. Run single drills first (`npx vitest run -c vitest.e2e.config.ts tests/e2e/scenarios/crash-drills.test.ts
      -t <name>` with the environment `tools/e2e.sh` sets), `null-write` first.
      **Done 2026-10-26:** 10/10 twice in a row. The first runs showed: every start crashed (`__start__` is 0 at run time), the
      "misaligned" record text, the HOME menu's "software was closed" note, and longer connection-reset spells (retries ~60 s).
- [x] Check after a main-thread drill that the console is back in **hbmenu** (D13's `exit()`), not the HOME menu, and that no
      `crash_reports` file appears a moment later (a thread left running into unloaded code would cause one: D33). If it does,
      write down what the record and creport say and propose the fix (likely `svcExitProcess` for that path: amends D13, owner's OK).
      **Done 2026-10-26:** it did, twice: hbmenu crashed under creport after our `exit()` (main thread: graphics left set up; worker:
      the parked thread's stack still mapped out of the heap). Fix with the owner's OK: D35, every crash on the Switch ends with
      `svcExitProcess` (after the crash screen if one shows); the HOME menu then says "The software was closed because an error
      occurred." The driver compares the crash folders once more at the end of every scenario.
- [x] The `build_id` in a device record equals the archive folder name (`~/build/suwayomi-nx/archive/<id>/`), and
      `tools/symbolize-crash/symbolize-crash.sh` turns its `pc` and stack into our functions (e.g. the null-write drill's line).
      **Done:** `6e57c322…` both, `pc` → `Drills.cpp:39`, stack → `main.cpp`.
- [x] The `oom` drill (Switch only): `std::bad_alloc` reaches the crash screen.
- [x] A Switch golden of the crash screen (`main-loop-exception` drill, `--update-goldens`, look at it), and switch the golden check
      on for the Switch in `crash-drills.test.ts` (desktop only for now).
- [x] The watchdog drill: the app ends ~10 s after the drill starts. Also check by hand once that pressing HOME and waiting 15 s
      in the HOME menu does **not** end the app (the watchdog pauses out of focus). **Done:** 10.2 s; 22 s out of focus, no watchdog.
- [x] Owner question: `appletSetCpuBoostMode` (D17) needs a D28 stand-in before its ban can be switched on (libnx's `_appletCleanup`
      references it). If the owner says yes: add it to `tools/safety/stand-ins.txt` + `src/app/SwitchStandIns.cpp`, uncomment the
      rule in `tools/safety/banned-symbols.txt`, record the OK in D28. **Done (owner: yes):** a stand-in can't reach a call inside
      libnx's own object file, so it got D34's `only-from=_appletCleanup` rule instead (owner OK), proven by two new gate probes.
- [x] Optional control case (SAFETY.md), **only if the owner says yes and watches**: the null-write drill with our handler disabled,
      in application mode. Expect a new `atmosphere/crash_reports` file, no `logs/crash-*.txt` of ours, no reboot screen. The gate
      refuses a build without our hooks, so this needs a one-off build without the gate: agree how with the owner first.
      **Done (owner watched):** a local, never-committed edit; the gate refused the build, the `.nro` was packed by hand, run once,
      then deleted and the normal build put back on the card. Result as expected: creport report for module `suwayomi-nx`
      (data abort at 0), no record of ours, no fatal, back on HOME.

## Acceptance
`crash-drills` green twice in a row on the console, zero new fatal or crash-report files, the record symbolizes, the open questions
answered (or recorded as declined).

## Sonnet-safe chunks
None (device behaviour, crash safety).
