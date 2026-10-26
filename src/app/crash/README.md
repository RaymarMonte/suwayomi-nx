# crash (crash containment, docs/SAFETY.md S1 and S8, D13, D33)

**Purpose:** a crash anywhere in the app ends as "app closed + crash record", never as Atmosphère's crash reporter or its
"press POWER to reboot" screen. Built in WP-03b.

**Files:**
- `Crash.hpp` the interface `main()` uses. `Crash.cpp` the shared logic: the record, how each crash ends, the watchdog.
- `CrashSwitch.cpp` libnx's hooks: `__libnx_exception_handler`, `__nx_exception_stack` (32 KB), our `diagAbortWithResult`.
  `CrashDesktop.cpp` the PC's stand-in: POSIX signals with a per-thread alternate stack, so everything can be tested on the PC.
  `CrashDetail.hpp` connects the two halves.
- `Drills.hpp/.cpp` the crash drills (test builds only).
- The pure parts are in `core/CrashRecord.hpp` (record format, log ring, fixed-size text) and unit-tested there.
- The crash screen is `src/ui/crash/CrashActivity` (borealis's own `Application::crash()` is an empty stub).

Why the libnx hooks are in the app and not in `platform/switch/`: libnx refers to `__libnx_exception_handler` only weakly,
and a linker never pulls a definition out of a static library for a weak reference. They have to be in the executable's own
object files, or libnx's defaults win silently. `tools/safety/check-elf.sh` checks that ours are linked.

## How a crash ends

`install()` runs first in `main()`. On the Switch the hooks are active from the process's first instruction anyway (S12);
`install()` notes the main thread, the module's address range and the build id, opens the crash file, and sets
`std::set_terminate`. Desktop: it installs the signal handlers.

| Where it happens | What catches it | How it ends |
|---|---|---|
| CPU crash (null pointer, misaligned access, stack overflow, ...) on the **main thread** | Switch: libnx's exception handler (on its own 32 KB stack). Desktop: a signal | record, then **Switch: the process ends at once** (D35). Desktop: the D13 exit: arm the 3 s exit deadline, run the exit steps (stop borealis's task thread), stop the watchdog, `exit(EXIT_FAILURE)`; desktop signals: `_exit(1)` |
| `std::terminate()` (an exception nobody caught, a `noexcept` broken) on the main thread | our terminate handler | same as above |
| `diagAbortWithResult` on the main thread | our override | GPU result (module 359, deko3d): `gpu_abort`, no crash screen. Other results: `abort`. Then as above |
| any of the above on **another thread** | the same handlers | record, then that thread **parks** (sleeps forever) and the main thread, which is fine, shows the **crash screen**; any button, a tap or 60 s closes it. Switch: then the process ends at once (D35: the parked thread must not outlive us into hbmenu). Desktop: normal close, exit code 1. GPU aborts there end the process at once |
| a C++ exception escapes one **main-loop iteration** (caught in `main()`) or a **worker task** (caught at its task boundary) | `recordMainLoopException()` / `recordWorkerException()` | record, crash screen, then like the row above |
| the **main loop stops for 10 s** while the app has the focus | the watchdog thread (S8) | record (`watchdog`), then the process ends at once (`exitProcessNow`): the stuck main thread can't do anything else |
| a **second crash** while one is being handled | the re-entrancy guard | the process ends at once, no second record |
| an exit after a crash that **hangs** for 3 s | the watchdog's exit deadline | the process ends at once |
| a crash **before `install()`** or after `finish()` | the hooks | Switch exception: the process ends at once. `diagAbortWithResult`: libnx's own behaviour, `svcBreak` (a "user break", never a system fatal; Atmosphère keeps a crash report with the result) |

"Ends at once" = `platform::exitProcessNow()`: `svcExitProcess()` on the Switch, `_exit()` on the PC. On the Switch the
console goes to the HOME menu, which says "The software was closed because an error occurred." (the system's note for a
process that ends without telling the applet manager). No crash reporter, nothing written, no fatal (seen on the console, WP-03c).

Why not `exit()` on the Switch (D35, found on the console in WP-03c): `exit()` hands the CPU back to hbloader, which starts
hbmenu in the same process and with the same heap. Whatever a crash leaves behind is then hbmenu's problem: after a main-thread
crash borealis's graphics were still set up (a crash skips their shutdown), after a worker crash the parked thread's stack was
still mapped out of the heap. Both times hbmenu crashed under Atmosphère's crash reporter. `svcExitProcess` lets the system
clean up everything. The desktop has no hbloader, so it keeps D13's `exit()` (D33).

Rules for code that runs after a crash (`fatal()`, the record writer, the platform hooks): no memory allocation, no borealis,
no locks the crashed code might hold. Everything uses fixed buffers and a file descriptor opened at start-up.

## Exit steps and threads (for later WPs)

Every thread we start calls `crash::prepareThread()` first and has an explicit stack size (S8). A worker pool registers a
stop function with `crash::addExitStep()` (at most 8, each must finish within about a second; only the desktop's crash exit
runs them now, D35) and catches exceptions at its task boundary with `crash::recordWorkerException()`. `main()` registers
`brls::Threading::stop`, calls `clearExitSteps()` when the main loop ends normally (borealis has stopped its thread itself by
then), and `endAfterCrashScreen()` after a crash screen. A pool must not wait for a thread that crashed: it is parked forever
(check `crash::crashScreenWanted()` before joining at shutdown).

## The crash record

`logs/crash-<UTC date>-<UTC time>-<n>.txt`, e.g. `logs/crash-20261025-153007-1.txt` (the console's clock may be wrong: compare
names, not dates). It's written into `logs/crash-pending.tmp`, which `install()` opens (empty) at start-up, then renamed.
Plain text, one `key: value` per line, so a person, the next launch (`core::crashrecord::parseCrashRecord`), the E2E harness
(`tests/e2e/lib/crashrecord.ts`) and `tools/symbolize-crash` can all read it:

```
suwayomi-nx crash record 1
type: cpu_exception            cpu_exception | abort | gpu_abort | terminate | main_loop_exception | worker_exception | watchdog
what: data abort at 0x0000000000000000 (null pointer?)
thread: main                   main | other | watchdog
version: 0.0.1 (test build)
platform: switch               switch | desktop
mode: application
build_id: 44401c13a4…          the .elf's GNU build id = the folder in ~/build/suwayomi-nx/archive/
uptime_ms: 1234
result: 0x002c3b67 (module 359, description 5661)        aborts only
module: base 0x0000000008000000 size 0x2a0000           where the app's code and data were
registers:                                               CPU crashes only
  pc: BASE + 0x1a2b            address inside the app = offset from its start (symbolize-crash looks it up)
  lr: BASE + 0x1b00
  sp: 0x…   fp: 0x…   far: 0x… (the fault address)   esr: 0x… (the CPU's reason code)
stack (return addresses, innermost first):
  #0 BASE + 0x1b00             Switch: the chain of frame records (x29), checked with the kernel at every step
  #1 0x0000007100012345        outside the app (system code): a plain address
log (last 64 lines, oldest first):
  510 W drill: null-write      milliseconds since start, level letter, message (from an in-memory ring buffer)
end of crash record            only written if everything above fitted: a record without it was cut short
```

To read the addresses: `tools/symbolize-crash/symbolize-crash.sh logs/crash-….txt` (inside WSL). It finds the `.elf` by build
id in `~/build/suwayomi-nx/archive/` (Switch) or the desktop build folders, and adds `-> function at file:line` under each line.
Our own code is built with `-fno-omit-frame-pointer` on the Switch so the stack walk finds every frame of ours.

## The next launch: safe mode

`install()` moves every `logs/crash-*.txt` into `logs/crashes/` (the newest 10 are kept) and reports the newest in `LastRun`.
A non-empty leftover `crash-pending.tmp` (a record whose rename never happened) is kept as `crash-<now>-0.txt`; an empty one
(the app was killed, not crashed) is deleted. If there was a record, this run is in **safe mode**: `app_start` says
`safeMode: true` and names the record, the cache folder is cleared, and a short note says the app closed unexpectedly last time.
WP-08's `makeRuntimeProfile()` takes the flag too (memory budgets halved, prefetch off) and the app won't restore the last screen.

## Drills (test builds, SAFETY.md "Crash drills")

The test config (`test/config.json`) names one, e.g. `{"drill": "null-write"}`. It starts the first time the app is idle, after
a `drill` event (`phase: "start"`). Scenario: `tests/e2e/scenarios/crash-drills.test.ts`, one test per drill.

On the Switch every drill except `cache-write-exit` ends with the HOME menu's "software was closed" note (D35); the desktop
ends with exit code 1.

| Drill | What it does | Expected end | Record |
|---|---|---|---|
| `null-write` | main thread writes through a null pointer | app closes by itself | `cpu_exception`, main |
| `misaligned` | Switch: an exclusive load from an odd address (always faults on ARMv8). Desktop: raises SIGBUS (x86 has no alignment faults) | closes by itself | `cpu_exception`, main |
| `terminate` | `std::terminate()` on the main thread | closes by itself | `terminate`, main |
| `gpu-abort` | `diagAbortWithResult(MAKERESULT(359, 4))`, what deko3d does on a GPU error. Desktop: the same handling, called directly | closes by itself, no crash screen | `gpu_abort`, main |
| `watchdog` | the main loop sleeps 15 s | the watchdog ends the process after ~10 s | `watchdog`, watchdog |
| `worker-null-write` | a worker thread writes through a null pointer | crash screen, A closes it | `cpu_exception`, other |
| `worker-stack-overflow` | a worker thread (256 KB stack) recurses until its stack runs out | crash screen | `cpu_exception`, other |
| `main-loop-exception` | a C++ exception escapes one main-loop iteration | crash screen | `main_loop_exception`, main |
| `oom` | `new` 2 GB blocks until the allocator says no (never touched) | crash screen (`std::bad_alloc`) | `main_loop_exception`, main. Switch only: the PC hands out address space almost without limit |
| `cache-write-exit` | a worker writes `cache/drill.bin` in 64 KB pieces and never finishes; then the app is asked to quit | normal quit within 1.5 s, the `.tmp` is dropped, no record (S5). Killed instead: the next start deletes the `.tmp` | none |

Every crash drill also checks the next launch: `safeMode: true`, the record filed in `logs/crashes/`.
