# Crash and brick safety

Priority order: **never brick** > **never crash the console** (the "press POWER to reboot" screen) > the app itself may crash,
but it should exit cleanly and leave a log behind.

## What can go wrong (plain words)

| Level | What the user sees | Acceptable? | Typical cause |
|---|---|---|---|
| 1. App crash | App closes, back to hbmenu or HOME | OK (fix it, but no harm done) | bug in our code |
| 2. System fatal | Atmosphère's fatal screen: "A fatal error occurred… Press POWER to reboot" | **Must not happen** | see "Why applet mode is dangerous" below |
| 3. Soft brick | Console won't boot into Atmosphère until the SD card is fixed on a PC | **Must never happen** | corrupting files Atmosphère needs, or filling the SD card |
| 4. Brick | System itself damaged | **Must never happen** | writing to internal storage (NAND), system saves/settings, installing/deleting titles or tickets |

## What the research found (2026-10-21)

1. **Atmosphère's crash reporter (`creport`) decides between level 1 and level 2.** After saving a crash report and terminating the process,
   it only skips the system fatal when the crashed process **is an application** or the crash was a **user break** (`svcBreak`).
   Anything else gets `fatalThrowWithContext(…)`, which is the reboot screen. (`stratosphere/creport/source/creport_main.cpp`)
   - **Application mode** (hold R on a game): an unhandled CPU crash stays at level 1. It's already fairly safe.
   - **Applet mode** (Album): we run inside a library applet, which isn't an application, so a CPU exception (null pointer, use-after-free,
     stack overflow, misalignment) **becomes level 2**. This is the main way a homebrew app "crashes the OS".
   - **Since D21 (2026-10-22) the app runs only in application mode.** Opened from the Album it only shows a short "open it with R" screen
     and exits, so that screen is the only code of ours that still runs in applet mode. S12 keeps it tiny.
2. **libnx lets an app catch its own CPU exceptions** by defining `__libnx_exception_handler` plus a dedicated `__nx_exception_stack`.
   Established homebrew does this: nxdumptool, Goldleaf, OpenMW, melonDS, and Atmosphère's own modules. nxdumptool logs registers and a stack
   trace (as offsets from the app's base address, so they can be symbolized on the PC), shows a borealis crash screen, and then calls `exit()`.
   A normal exit never reaches creport, so there's no system fatal.
3. **`diagAbortWithResult` is a weak libnx function** (default: `svcBreak(BreakReason_Panic)`), so apps can replace it. That matters because:
   - **deko3d (borealis's Switch GPU backend) calls it on any "unrecoverable system/GPU error" even in release builds**, including GPU
     memory block creation failures (`dk_device.cpp`: `RaiseError → diagAbortWithResult(MAKERESULT(359, result))`).
   - libnx calls it when start-up service init fails.
   - nxdumptool overrides it to log, show its crash screen, and exit cleanly. We do the same.
4. **borealis itself never calls `fatalThrow`** (code search), so the risk is in libraries below it and in our own code.
5. **SD card corruption** is mostly an exFAT problem. Nintendo's exFAT driver has no backup allocation table, and corruption happens when
   homebrew is *writing* when it crashes, gets force-closed (HOME → close), or the console loses power. FAT32 is the community recommendation.
   Fewer, atomic, short writes cut the risk on both.
6. **hbloader limitation we can't fix:** opening the Album over some suspended games can fatal inside hbloader itself
   (nx-hbloader issue #21), before our code runs, so even our applet-mode guard (S12) can't prevent it. Users should close the game first.
   Application mode avoids it, and it's the only mode the app supports (D21).
7. **sys-autopilot is a system module.** If *it* crashes, that's a system fatal, and our app can't prevent it. Keep it updated, and
   optionally disable it (remove its `boot2.flag`) when you aren't testing.

## The safety rules (S1–S12)

**S1 Crash containment** (`src/app/crash/`, built in WP-03b; how each kind of crash ends, the record format and the drills:
`src/app/crash/README.md`, D33. The desktop build has the same logic on POSIX signals, so it's tested on the PC too):
- Define `__nx_exception_stack` (32 KB) + `__nx_exception_stack_size` + `__libnx_exception_handler`.
- Override `diagAbortWithResult`, call `std::set_terminate`, and catch `...` at the main loop and at every worker task boundary.
- The handler has a re-entrancy guard (second entry → `svcExitProcess()` immediately). It writes a small crash record (type, PC/LR/SP/FAR
  relative to module base, ~32-frame stack trace, build id, mode, last 64 log lines from an in-memory ring buffer) to `logs/crash-<time>.txt`
  using a file descriptor opened at start-up (no allocation in the handler). Then the process ends with `svcExitProcess()`
  (D35, amends D13's `exit()`; the desktop keeps `exit(EXIT_FAILURE)`). A crash on another thread parks that thread and the main
  thread shows the crash screen first. None of them reaches creport; the HOME menu then says "The software was closed because an
  error occurred." (WP-03c found on the console that `exit()` back to hbmenu let hbmenu crash over what the crash left behind.)
- borealis crash screen (`brls::Application::crash(msg)`): only for non-GPU aborts, and only if the UI is healthy. A GPU-origin abort
  (result module 359) skips straight to exit.
- Next launch: a crash file present → short "the app closed unexpectedly last time" note + **safe mode** for that session
  (memory budgets halved and prefetch off, caches cleared, last screen not restored).

**S2 No fatal calls, ever.** A build gate (`tools/safety/check-elf.sh`, runs `nm` on the final `.elf`) fails if the binary references
`fatalThrow`, `fatalThrowWithPolicy` or `fatalThrowWithContext`. It also fails if our `__libnx_exception_handler` / `diagAbortWithResult`
aren't the ones linked (libnx's weak defaults would be a regression), or `__nx_exception_stack` isn't ours and ≥ 32 KB.
Since WP-03b it runs inside the Switch build before `elf2nro`, so a failing `.elf` never becomes a `.nro` (locally and in CI);
`tools/safety/gate-selftest.sh` proves it fails on a deliberately linked `fatalThrow` and `setsysSet*` call, and on a call to
or a stored pointer to `appletSetCpuBoostMode` (D34).

**S3 Brick guard: banned system APIs.** The same gate fails on references to anything that writes the system:
`fsOpenBisFileSystem`, `fsOpenBisStorage`, `fsOpenSaveDataFileSystemBySystemSaveDataId`, `fsCreate*SaveData*`, `fsDelete*SaveData*`, `fsFormat*`,
`setsysSet*`, `setSet*`, `nsDeleteApplication*`, `ns*Install*`, `ncmContentStorage{WritePlaceHolder,Register,Delete}*`, `ncmContentMetaDatabase{Set,Remove}*`,
`esImportTicket*`, `esDeleteTicket*`, `splSetConfig`, `bpcRebootSystem`, `bpcShutdownSystem`, `spsmShutdown`, `pcvSet*`, `clkrstSet*`,
`apmSetPerformanceConfiguration`, `lblSet*`. Exceptions go in `tools/safety/allowed-symbols.txt`, which needs the owner's OK plus an Opus review.
(WP-01 ran the scan on the plain Hello app: `docs/safety-baseline.txt`, list in `tools/safety/banned-symbols.txt`. Two hits, both pulled in
by libraries and never called. They get do-nothing stand-ins through `ld --wrap` instead of allowlist entries, D28, built in WP-03b:
`tools/safety/stand-ins.txt`, `src/app/SwitchStandIns.cpp`. D17's `appletSetCpuBoostMode` is banned too, with D34's `only-from`
rule: libnx's exit code calls it from inside the same file (only to undo a boost nobody set), which a stand-in can't reach, so the
gate fails on any reference to it except that one call.)

**S4 Filesystem jail.** All writes go through `platform::SafeFs`, which refuses any path outside `sdmc:/switch/suwayomi-nx/`
(desktop: `./data/`). It normalises `..`, `//`, case and the `sdmc:` prefix, and is unit-tested against traversal tricks and `sdmc:/atmosphere`,
`sdmc:/bootloader`, `sdmc:/config`, `sdmc:/hbmenu.nro`. A CI grep fails on `fopen(…"w"/"a")`, `std::ofstream`, `rename`, `remove`,
`mkdir` outside `SafeFs` (`tools/safety/check-writes.sh`, run by `tools/build.sh tests`). Built in WP-03b: `src/platform/SafeFs`,
path rules in `core::jailPath`. It also refuses any `.nro` (no self-update, D15).

**S5 Write discipline (SD/exFAT safety).**
- Atomic writes: `x.tmp` → write → `fflush` → `fsync` → close → `rename` over `x`. Config also keeps `x.bak`.
- Few, small writes: config saves debounced (≥ 2 s), reading progress goes to the *server* (not the SD), disk-cache files are written once
  and never edited.
- No file stays open between frames.
- On exit request / HOME / sleep: stop new writes, finish or abandon in-flight cache writes (temp files are just deleted next start),
  flush config, all within 1.5 s. borealis already calls `appletLockExit()`, so the system waits for us.
- Free-space guard: never write to the cache if the SD would drop below 1 GB free (configurable). The cache is disposable, so corrupt
  entries are deleted and re-downloaded.

**S6 Untrusted input** (server JSON and images scraped from the web by extensions):
- Response caps: JSON 8 MB, image 32 MB. JSON nesting-depth limit.
- Image header checked **before** decoding: each side ≤ 16384 px and `w×h×4` ≤ the profile's max decode bytes. This guards against
  "decompression bombs" (a tiny file that expands to gigabytes).
- Parsers return errors and never throw across module boundaries.
- **Fuzz tests** (libFuzzer + ASan/UBSan on host) for JSON→models, GraphQL responses, image sniff+decode, and config loading.

**S7 GPU safety.** Textures are tiled ≤ 2048 px and created only on the main thread. A GPU-memory budget is checked before creating
textures. Only release deko3d is shipped. Any remaining deko3d failure goes through our `diagAbortWithResult` → clean exit (level 1).

**S8 Resource hygiene.** Explicit thread stack sizes (workers 256 KB, decoders 1 MB), bounded queues, no unbounded recursion.
A **main-loop watchdog** thread logs and exits cleanly if the UI loop stalls for > 10 s, instead of leaving the user stuck.

**S9 Power/thermal.** Never touch clocks or performance modes (also in S3). vsync frame pacing. Keep-awake only inside the reader.

**S10 Test-tool safety.** The E2E harness talks to sys-autopilot through an **allowlist**: `GET /status`, `GET /screenshot`, `/input/*`
(this includes our fork's `/input/touch` and `/input/swipe`, D19), `/controller/*`; `GET /files` (and `/files/hash`) only under our folder, `atmosphere/crash_reports`, `atmosphere/fatal_errors`,
`atmosphere/fatal_reports` (added in WP-03, owner OK 2026-10-25); `PUT`/`DELETE /files` only
under `/switch/suwayomi-nx/`. **Never**: `/install`, `POST /settings/*`, `POST /network/dns`, `/settings/airplane`. The allowlist is unit-tested
(`tests/e2e/drivers/autopilot.ts` + `.test.ts`): paths are checked segment by segment (no `..`, `.`, `//`, backslashes, look-alike folders).

**S11 No self-update in the MVP.** If we add it later: download to a temp file, verify size + SHA-256, atomic rename, keep the previous
`.nro` as `.bak`, and touch nothing else.

**S12 Applet-mode guard** (D21, built in WP-08). The app refuses applet mode, but the refusal screen itself runs in applet mode, where a crash
would be a system fatal. So that path stays tiny:
- It runs first in `main()`. Only the logger and S1's set-up (pre-opened crash-log file, log ring buffer) start before it.
  No borealis, GPU (deko3d), network, image or config code runs.
- It writes its one log line (and the `launch_refused` test event) *before* showing anything, and writes nothing after. So closing the app
  from HOME at any moment is harmless.
- It draws with libnx's text console (a plain framebuffer, no GPU setup, no fonts to load), reads buttons and touch, closes on any button,
  a tap, or after 60 s, and then returns from `main()` normally.
- If any step fails (console or input setup), it skips the screen and exits. Nothing in it may abort, throw or call `fatalThrow`.
- S1's crash handler is linked in and active from the first instruction, so even a bug here ends as "app closed + crash log".
- Tested by the `applet-guard` scenario: desktop (`--simulate-applet-mode`) and the console via the Album, including one null-pointer drill
  inside the screen (WP-08). The release build is checked from the Album in WP-12.

## How safety is tested

| Test | Layer | What it proves |
|---|---|---|
| `check-elf.sh` (S2/S3) | build gate, every build | no fatal calls, no system-writing APIs, our handlers are linked |
| SafeFs + allowlist unit tests (S4/S10) | unit | can't write outside our folder; harness can't call dangerous endpoints |
| Fuzz targets (S6) | host, 60 s each in CI, longer on demand | parsers/decoders survive garbage |
| Atomic-write kill test (S5) | desktop | kill the app mid-save 100× → config always loads |
| **Crash drills** | device, **application mode** (+ one drill inside the S12 guard in applet mode) | see below |
| fatal-errors gate | every device scenario | any new file in `atmosphere/fatal_errors/` or `atmosphere/fatal_reports/` = **hard failure, stop the run, tell the owner** (the harness writes `tests/e2e/out/STOP-console-fatal.txt` and won't run again until the owner deletes it) |

**Crash drills** (test builds only, triggered through the test config): null-pointer write, misaligned access, stack overflow on a
worker thread, `std::terminate`, `diagAbortWithResult` (simulated GPU error), 2 GB allocation, and an exit request in the middle of a cache write.
For each one, in application mode (D21), the drill passes only if: the app exits, `logs/crash-*.txt` exists and can be parsed,
**no new `atmosphere/fatal_errors` or `fatal_reports` file appears**, no new `atmosphere/crash_reports` file appears (not even a moment
later: the driver compares once more at the end of the scenario), the HOME menu shows "The software was closed because an error
occurred." (screenshot check, D35), sys-autopilot still answers,
and the next launch enters safe mode. The null-pointer drill also runs once inside the applet-mode guard screen (S12, WP-08), because that's
the one place where a crash would still be a system fatal. Same pass rules (the safe-mode check uses the next launch in application mode).
**Optional control case**, only with the owner's explicit OK and the owner watching: the same null-pointer drill with our handler
disabled, in application mode. It's expected to leave a new `atmosphere/crash_reports` file and *no* `logs/crash-*.txt` of ours, and no reboot
screen (creport doesn't fatal applications), which proves our handler is what catches the crash. It's run only once, with nothing else writing to the SD.
(Before D21 this ran in applet mode and was expected to show the reboot screen. That version moves to WP-21.)
