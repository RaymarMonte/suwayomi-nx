# WP-21 Applet mode: open the app from the Album (a safe crash ending, the Lite profile, the tip)

Model: **Opus** (D47; never Sonnet: crash safety, memory, the console) · Effort: max · Size: **L** (split point below) ·
Needs: WP-12, WP-18, WP-18b (all done) · Owner: answers given 2026-11-20 (below); **watch the first console steps** (the
crash-ending proof can need a reboot by you), dock the console once, at the end try it from the Album

## Goal
After this WP the app **opens from the Album** (applet mode) and works there, with less memory: the `Lite` profile (D12)
gives smaller caches and prefetch, so pages load a little slower but nothing runs out. First, and before anything else, a
**crash from the Album ends safely**: today any crash of ours there makes the HOME menu stop the whole system ("press POWER",
D41). The fix is to tell the system "I'm leaving" before ending, the way quitting the Homebrew Menu does. It is proven on the
console with test crashes while the owner watches, and **if it can't be proven, the app keeps refusing the Album** (D21
stays, owner's answer 1). Then: the refusal screen becomes a tip at every Album start, the memory is measured, the keyboard,
HOME, sleep, docking and the network are checked from the Album, WP-18's downloads and WP-18b's offline start work there,
and **every test scenario runs in both modes**, on the PC and on the console.

## Owner's answers (2026-11-20)
1. **If the crash ending can't be proven: keep refusing the Album.** At most **3 tries** (each failed try is a system fatal
   the owner reboots from). Then the app stays application-mode only (D21), and what was tried is written down (D62).
2. **The tip: at every start from the Album, with "Don't show again".** A dialog with two buttons, **OK** and **Don't show
   again**. The text (shown to the owner in the question): "Opened from the Album, Suwayomi NX has less memory, so pages load
   a little slower and fewer are kept ready. For the full app: on the HOME Menu, hold R and start any game. Keep holding R
   until the Homebrew Menu appears, then choose Suwayomi NX."
3. **Unattended runs after the proof.** Once the test crashes from the Album passed with the owner watching (P1 and P2
   below), the other applet-mode console runs go like any Switch run. A fatal still stops the harness
   (`STOP-console-fatal.txt`) and the console waits for the owner.
4. **All Switch scenarios in both modes** (about 1.5 to 2 hours more console time). Size L; the split point below.

## What the planner found (2026-11-20; sources read, nothing run on the console)
- **Why a crash from the Album is a fatal, and what a normal Album exit does** (libnx `services/applet.c`, nx-hbmenu
  `nx_main/main.c`, nx-hbloader `source/main.c`, all master; the installed libnx is 4.12.0):
  - hbmenu, when the user quits it, sets `__nx_applet_exit_mode = 1`. libnx's `appletExit()` then makes the process's last
    step `_appletExitProcess`, which sends **`ILibraryAppletSelfAccessor` command 10, `ExitProcessAndReturn`** (an
    application sends `ISelfController` command 0, `Exit`) and then **sleeps until the system ends the process**. Only if
    that message fails does it call `svcExitProcess`.
  - Our crash ending (D35) calls `svcExitProcess` without that message. In application mode the HOME menu then only shows
    "The software was closed because an error occurred." (D35). From the Album the applet manager reports the Album as
    "exited abnormally" and the HOME menu (qlaunch) stops itself: `0x290` = `la::ResultLibraryAppletExitedAbnormally`, the
    "press POWER" screen (D41).
  - libnx makes the session public: `Service* appletGetServiceSession_LibraryAppletSelfAccessor(void)` (in the installed
    4.12.0 header). So the crash path can send command 10 itself with `serviceDispatch(session, 10)`: no memory allocation,
    no lock, it uses the calling thread's own IPC buffer. That's the whole fix idea; whether the applet manager accepts it
    from a process that just crashed is what P1 proves.
  - hbloader itself never initialises the applet service and never exits by itself; the homebrew it loads (hbmenu, us) has
    its own applet session, and the process ends through that.
- **Memory from the Album** (nx-hbloader `setupHbHeap`): the heap a homebrew gets = what the process may use, minus what it
  already uses, minus 2 MiB, minus Atmosphère's `[hbloader] applet_heap_reservation_size` (default `0x8600000` = 134 MiB,
  kept free for other applets, the **keyboard** among them) unless `applet_heap_size` is set. The owner's
  `atmosphere/config/system_settings.ini` (SD backup 2026-10-22) leaves both at their defaults. Other homebrew report about
  380-400 MB in applet mode (application mode: 3,184 MiB here). A game running behind the Album may leave less: measured.
  The app never changes these settings (D14; users wouldn't either).
- **The keyboard is a library applet of its own**, from the same memory: Checkpoint's release notes say low memory in applet
  mode sometimes kept the system keyboard from showing. borealis's `swkbdShow` failing returns false (no crash); the 134 MiB
  reservation is what leaves it room.
- **Network:** borealis gives applet mode 2 BSD sessions and `sb_efficiency = 1` (smaller socket buffers; application mode:
  12 and 8). Our net runs one network thread with a curl multi handle (one socket call at a time), so 2 sessions are enough;
  how fast pages arrive is measured.
- **Exit request:** borealis's applet hook answers `AppletHookType_OnExitRequest` with `Application::quit()` but leaves
  `__nx_applet_exit_mode` at 0, so `main()` returning hands the process back to hbloader, which loads hbmenu again, although
  the system asked the Album to close. Application mode does the same today without trouble; from the Album the app sets exit
  mode 1 on an exit request (borealis has `Platform::exitToHomeMode(true)`), so it leaves through command 10 like hbmenu.
- **Still unknown, so checked on the console:** whether the Album stays open behind the HOME menu; whether the OnResume hook
  fires for a library applet (D61's wake check needs `appletSetRestartMessageEnabled`); whether `appletSetMediaPlaybackState`
  keeps the screen on; docked output; page and download speed with 2 small sockets; memory with a game behind the Album.

## Planner's defaults (the owner can change them after trying; record them all in D62)
**1. The crash ending from the Album** (`platform::exitProcessNow`, Switch; the crash module's Switch endings go through it:
main-thread crashes, the crash screen's end, the watchdog, a second crash, a hanging exit; check every caller):
- **Applet mode:** send command 10 on `appletGetServiceSession_LibraryAppletSelfAccessor()`; if it succeeded, wait for the
  system to end the process (sleep in a loop, at most `crashExitWaitMs`, 5 s); if the call failed or the wait ran out,
  `svcExitProcess()` as today (a fatal can't be avoided then). A crash inside that call is a second crash: the re-entrancy
  guard goes straight to `svcExitProcess`. `crash::install()` runs before the profile exists (main's order), so the wait
  starts as 5 s and main() hands the profile's value to the crash module right after building the profile (a plain
  variable: nothing is allocated or locked after a crash).
- **Application mode: unchanged** (D35, proven by the drills; the "software was closed" note stays as the sign of a crash).
- **Desktop: unchanged** (`_exit`, exit code 1). The mode reaches the platform from `crash::install(fs, mode)`.
- A normal quit (PLUS) from the Album still returns to hbmenu, like every homebrew. An **exit request** from the system (a
  game started, the Album closed) sets exit mode 1 first, so the process leaves through command 10 (above).

**2. The proof, owner watching** (`SNX_APPLET_CONSOLE=1`, D41's opt-in, until P2 passed). The reboot after a fatal: POWER,
then Hekate → Launch → CFW (EMUMMC); read the fatal report, then delete `tests/e2e/out/STOP-console-fatal.txt`.
- **P1, the smallest case first:** while the refusal screen still exists, exactly D41's case: from the Album, the null-pointer
  drill inside the refusal screen, now with the new ending. **Pass:** the Album closes to the HOME menu, no new file in
  `atmosphere/fatal_errors` / `fatal_reports` / `crash_reports`, our crash record exists and parses, sys-autopilot still
  answers, the Album opens again. Note what the HOME menu shows (a note or none).
- **If P1 fails:** at most two more variants, one try each (owner's answer 1: 3 tries in all). Candidates, cheapest first:
  libnx's own sequence (`__nx_applet_exit_mode = 1`, then `appletExit()` and the exit function libnx set, which also closes
  the other applet sessions before command 10); the same after closing the display layer. Still a fatal → **stop**: the
  refusal stays (D21), the new code is removed unless it is provably harmless, D62 says what was tried; WP-21 ends there and
  the roadmap moves on to WP-16.
- **P2, the whole drill set from the Album** (after the Lite start below exists): `crash-drills` in applet mode, every drill
  (main thread, worker thread with the crash screen, `std::terminate`, GPU abort, watchdog, stack overflow, out of memory).
  **Pass:** each the P1 checks, plus the next start from the Album runs in safe mode. A fatal → stop the run, read our record
  and the fatal report; one fix that clearly matches the cause may be tried once more with the owner; otherwise back to
  answer 1.

**3. Starting from the Album:** the guard (S12) no longer refuses applet mode. It stays, with the same tiny libnx-console
screen and S12's rules, for one case: **too little memory** (`freeMem` at start below `liteMinFreeBytes`, a new profile
value; first guess 160 MiB, set from the measurement, enough for borealis plus the budget floors). Text (planner's): "Suwayomi
NX can't open from the Album right now: there isn't enough free memory (a game open behind the Album takes some). Close the
game and try again, or open Suwayomi NX with R: on the HOME Menu, hold R and start any game. Keep holding R until the
Homebrew Menu appears, then choose Suwayomi NX. Press any button or tap the screen to close." Event `launch_refused` gets
`reason: "low_memory"`. Otherwise the app starts as usual with the `Lite` profile.

**4. The Lite profile** (`makeRuntimeProfile(Applet, …)`; named `lite`, D12's name, instead of today's `applet`). Today's
placeholder values are the start; the measurement (step 6) sets the numbers. First guesses for a ~380 MiB heap:

| Setting | Lite (first guess) | Full (unchanged) |
|---|---|---|
| Image cache (decoded pages) | 25 % of free, ≤ 64 MiB | 40 %, ≤ 256 MiB |
| Covers / textures / one decode | ≤ 16 / ≤ 64 / ≤ 64 MiB | ≤ 64 / ≤ 256 / ≤ 128 MiB |
| `maxZoom` / `maxDecodeScale` | 2 / 2 (ARCHITECTURE's Lite column; today's placeholder says 1: blurry when zoomed) | 4 / 4 |
| Paged prefetch | −1 / +1, no next chapter, no background prefetch | −1 / +3, 2 pages, on |
| Strip | tiles ±1 row, decode 0 screens above / 1 below, 1 zoomed page | ±4, 1 / 3, 2 |
| Transfers / decode threads | 2 (like the 2 sockets) / 1 | 6 / 2 |
| Search all | 1 source at a time | 3 |
| Switch downloads | one chapter, **one page at a time** (D57) | one chapter, 2 pages |
| Space for downloads, SD cache | the same (they are on the SD card, and the setting is shared) | |
| Extra UI effects (`heavyUiEffects`) | off | on |
| New: `liteMinFreeBytes`, `crashExitWaitMs` | 160 MiB, 5 s | –, 5 s |

Safe mode in Lite halves the memory budgets as in Full, never below the floors. Every other limit stays as in Full unless the
measurement shows a reason (then say so in D62). `maxImageBytes` stays 32 MiB: one page at a time keeps the peak at one page.

**5. The tip** (owner's answer 2): at every start from the Album, after the main screen is up and before setup / login / the
"closed unexpectedly" note open over it, a dialog with the text above, buttons **OK** and **Don't show again**, touch and
buttons (D16). "Don't show again" saves `appletTip: false` in `config.json` (shared with application mode, harmless there).
Settings gets one switch to bring it back: "Show the Album tip" (on by default). Never shown in application mode. Settings →
About already shows the mode and the profile; its words become "Opened from the Album (applet mode) · Lite profile". Test
event `applet_tip` (`shown`, `ok`, `hidden`); the test config's `appletTip: false` keeps it away in every scenario except
`applet-start`.

**6. The console checks** (one session, owner watching, after P1; the desktop work comes first):
- **Memory:** the heap from the Album with nothing running behind it, and with Spelunky 2 suspended behind it; borealis's
  start cost in applet mode (`app_start`, `mem` events); after the soak. Note: opening the Album over some suspended games
  can fatal inside hbloader itself, before our code runs (nx-hbloader issue #21, SAFETY.md finding 6): if that happens with
  Spelunky 2, the fatal report names hbloader, not us; record it and don't repeat that case.
- **Keyboard:** setup's address field, the library search, with the image cache full. It must never crash (borealis's
  `swkbdShow` failing just returns false; the watchdog's `BlockingWait` as in D45). If it can fail to open, the field says
  "The keyboard couldn't open. Close a game behind the Album, or open Suwayomi NX with R." borealis returns the same false
  for "couldn't open" and "the user cancelled", so telling them apart may need the result code: changing borealis's code
  (D1, a pinned submodule) needs the owner's OK; without it, D62 records that the field stays silent.
- **HOME and back; sleep and wake; starting a game while the app is open** (an exit request: the app closes within S5's 1.5 s,
  config, the waiting-save queue and the download queue saved, no fatal). If OnResume doesn't fire for a library applet, the
  wake check (D37 connection reset, D61) uses the focus event instead; say which in D62.
- **Screen stays on** in the reader and on the Switch downloads tab (S9); **docked**: the reader turns landscape (WP-22) and
  the screen fills (the owner docks once); **network:** reader-paged's page times and one chapter download, compared with
  application mode; **touch** works.

**7. Tests in both modes** (owner's answer 4):
- `tools/e2e.sh desktop|switch --mode applet [scenario]`: every launch from the Album. Desktop: `--simulate-applet-mode`
  plus `--mem-limit=<the measured Album heap>`, so its budgets match the console's. Switch: HOME → close the software that is
  running (title takeover leaves "Spelunky 2" = hbloader running) → Album → hbmenu ("Applet Mode" badge) → the app; a new
  `profile.json` step. `SNX_APPLET_CONSOLE=1` goes away after P2 (answer 3).
- Goldens: one set where a screen looks the same in both modes (most); applet goldens only where it differs (the tip,
  Settings → About, the low-memory screen). Update goldens on both drivers after UI changes (WP-17's lesson).
- Scenarios: `applet-guard` becomes **`applet-start`** (the tip at every start, OK, Don't show again then no tip at the next
  start, the Settings switch brings it back, About's words; desktop also: the low-memory screen with a tiny `--mem-limit` and
  the drill inside it, desktop only). `crash-drills` runs in applet mode (P2). `soak` and `keyboard-wait` in applet mode.
  `low-memory` (desktop) in applet mode too. Every other scenario runs unchanged in both modes.

**Not now:** HTTPS, anything about the hbloader settings, an hbmenu forwarder, changing the application-mode crash ending.
WP-08b's "all controllers merged" stays its own WP.

## Read first
- `docs/STATUS.md`, this file. D12, D21, D35, D41, D57, D61 in DECISIONS.md; SAFETY.md S1, S5, S12 and "How safety is
  tested"; ARCHITECTURE.md "Launch modes and runtime profiles"; TESTING.md "Modes" (the matrix) and scenario 11.
- Interfaces only: `src/core/RuntimeProfile.hpp`, `src/core/RuntimeInfo.hpp`, `src/platform/Platform.hpp` + README,
  `src/app/crash/README.md` + `Crash.hpp`, `src/app/AppletGuard.hpp`, `src/app/AppContext.hpp`, `src/app/README.md`
  (main's order), `src/ui/README.md`, `src/core/AppConfig.hpp`. `.cpp` files only where this WP changes them:
  `platform/switch/SwitchPlatform.cpp`, `app/main.cpp`, `app/AppletGuard.cpp`, `app/crash/CrashSwitch.cpp`,
  `core/RuntimeProfile.cpp`, `ui/main/SettingsTab.cpp`, `ui/main/MainActivity.cpp`.
- E2E: `tests/e2e/README.md`, `drivers/Driver.ts`, `drivers/SwitchDriver.ts` (`toHbmenuAppletMode`), `drivers/DesktopDriver.ts`,
  `device/profile.json`, `scenarios/applet-guard.test.ts`, `scenarios/crash-drills.test.ts`, `tools/e2e.sh`.

## Deliverables
**Stage A: the ending, proven in the smallest place (do this first: it decides whether the rest happens)**
- [x] `platform::exitProcessNow` (Switch) in applet mode: command 10, the bounded wait, `svcExitProcess` as the last resort;
      the mode from `crash::install`; `crashExitWaitMs` in the profile. Application mode and desktop unchanged. Gate PASS.
      (2026-11-20; plus the ending note `logs/crash-ending.tmp`, D62 "as built, Stage A". P1 =
      `SNX_APPLET_CONSOLE=1 SNX_APPLET_DRILL=1 tools/e2e.sh switch applet-guard`.)
- [x] **P1 on the console, owner watching** (above). Pass → go on. Fail → ≤ 2 more variants; all fail → stop (answer 1).
      (2026-11-21: passed, 2 runs, no fatal, no note on the HOME menu; the system ends the process inside command 10.
      D62 "P1 passed".)

**Stage B: the app from the Album (PC first)**
- [x] main(): applet mode starts with the Lite profile; the guard only for too little memory (`liteMinFreeBytes`,
      `reason: "low_memory"`), S12's rules kept for that screen.
- [x] The Lite profile (`lite`), safe mode in Lite, the two new values; unit tests (suite `core`): budgets from Album-sized
      inputs, safe mode, floors, the low-memory threshold, Full unchanged.
- [x] Exit request from the Album → exit mode 1 (a platform hook beside the resume hook; Switch only).
- [x] The tip (model + dialog), `appletTip` in config (unit tests for the config field and the "show it?" rule), the Settings
      switch, About's words, the `applet_tip` event.
- [ ] e2e: `--mode applet` for both drivers, the "close running software" step, `applet-start` (replaces `applet-guard`),
      applet goldens where screens differ. **Desktop: every scenario green in both modes.**

**Stage C: the console from the Album**
- [ ] **P2: `crash-drills` in applet mode, owner watching**; then drop `SNX_APPLET_CONSOLE=1` (answer 3).
- [ ] The console checks (step 6), owner watching for the game-behind-the-Album case and docking; numbers into D62. What
      they find gets fixed here (the keyboard's failure text if it can fail, the wake signal, the exit request).
- [ ] Lite numbers set from the measurement; `soak` from the Album: memory flat, peak below the pressure line (85 % of the
      heap), no `mem_pressure`, no error; frames like application mode or the difference written down.
- [ ] **Switch: every scenario green from the Album** (and still green in application mode for anything this WP touched).

**Docs (as built)**
- [ ] D62 "as built" (with the console numbers); D21 and D41 marked amended; SAFETY.md S1 (the applet ending), S12 (now the
      low-memory screen), the drills table (applet mode); ARCHITECTURE.md's launch-mode section with Lite's real numbers;
      `src/app/crash/README.md`, `src/platform/README.md`; TESTING.md "Modes" (the two-mode matrix, `--mode applet`),
      scenario 11 → `applet-start`; `tests/e2e/README.md` point 6; RESEARCH.md's applet section updated; CLAUDE.md's "Rules
      for code" line about application mode only; ROADMAP's "Application mode only (D21)" paragraph; CHANGELOG.md ("opens
      from the Album").

## Acceptance
- P1 and P2 passed on the console with the owner watching: no new file in `atmosphere/fatal_errors`, `fatal_reports` or
  `crash_reports` for any drill from the Album; every record parses; the next Album start is in safe mode.
- `tools/build.sh tests` green (new Lite and tip tests included); Switch build 0 warnings, gate PASS; `tools/release.sh`
  dry run still passes (the ending is in the release build too).
- `tools/e2e.sh desktop` and `tools/e2e.sh desktop --mode applet`: every scenario green.
- `tools/e2e.sh switch` and `tools/e2e.sh switch --mode applet`: every scenario green (WP-21b's share, if split, there).
- Numbers in D62: the Album heap (alone, with a game behind), start cost, the soak's peak, page times and one chapter
  download (both modes), the keyboard, HOME / sleep / exit-request / docked findings.
- The owner's try: opened from the Album on the console, a chapter read on the owner's own server (the test build with the
  owner's config, D18 rules, or a normal build afterwards), and quitting back to hbmenu.

## Split point → WP-21b (only if one session can't finish)
Stop at a clean, green point after Stage A, Stage B (desktop green in both modes), P2, the console checks, the Lite numbers
and these Switch scenarios from the Album: `smoke`, `library-browse`, `manga-details`, `reader-paged`, `reader-webtoon`,
`reader-zoom`, `keyboard-wait`, `crash-drills`, `soak`, `applet-start`. **WP-21b** = WP-18 / WP-18b in applet mode on the
console (`switch-downloads`, `switch-download-queue`, `server-downloads`, `offline-reading` with the offline start from the
Album, `offline-errors`, `resume-progress`) plus the rest of the Switch matrix from the Album (`browse`, `library-tools`,
`updates-history`, `touch-pass`, `setup-first-run`, the reader's portrait and spread scenarios, `net-hello`, `auth-matrix`,
`broken-images`), and any Lite tuning those runs show. Add the WP-21b row to ROADMAP.md and say so in STATUS.

## Sonnet-safe chunks (Opus WPs)
- The `tools/e2e.sh --mode` flag plumbing and the README / TESTING.md text for it, from a tight spec; Opus reviews.
- The docs sweep at the end (the list above), after D62 is written; Opus reviews.
- Nothing in the crash ending, the profile, main()'s start order, the platform hooks or the console runs.

## Stop rules (read before any console step)
- Until P2 passed: every applet-mode console step with the owner watching. After it: answer 3.
- A fatal = stop; the harness's STOP file stays until the owner deleted it; never start the next try without the owner.
- 3 failed tries of the ending (P1 and its variants) = the end of WP-21 (answer 1).
- Before ending or pausing a turn that used the console: HOME menu → power button → Sleep.
