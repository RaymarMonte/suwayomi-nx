# WP-00 Device loop spike

Model: **Opus** · Size: M · Needs: 00b · Owner: **present** (console, SD card, handheld touch check)

## Goal
Prove the whole loop on the real console before writing app code: build a borealis demo for desktop and Switch,
deploy it with sys-autopilot-nx, launch it, press buttons, tap the screen, take a screenshot, and read a log file back. Record every
console-specific fact the later WPs depend on. (Toolchain and backups were done in WP-00a, our sys-autopilot build in WP-00b.)

## Read first
`docs/SETUP.md` (section B), `docs/TESTING.md` (Drivers section), `tools/sys-autopilot-nx/README.md`.

## Deliverables
- [x] Borealis desktop/Switch build dependencies installed (whatever WP-00a's base install didn't cover). Versions written back into SETUP.md
      → 4 apt packages + `switch-glm` (SETUP.md A.2, `tools/spike/wp00-install-deps.sh`). borealis commit `5f08b28`
- [x] `borealis_demo` (from xfangfang/borealis) builds and runs on desktop via WSLg (owner sees a window) → GLFW + GL3, owner OK
- [x] Desktop demo: check that the mouse works as a finger (tap, drag-scroll) under WSLg. D16 (touch first) relies on it
      → owner: all works. Also under Xvfb with xdotool (tap = 100 ms press; `click` is too short). TESTING.md DesktopDriver
- [x] `borealis_demo.nro` builds for Switch with deko3d → 4.5 MB, 0 warnings, `tools/spike/build-demo.sh`
- [x] From WSL: `curl /status` works with the token. Pick the address that works from WSL (IP vs `.local`) → IP (0.1 s; `.local` now resolves but 1.1 s)
- [x] Upload the .nro with `PUT /files`, verify SHA-256 with `/files/hash` → 4.5 MB in 3 s, hash matches
- [x] Find and record the button sequence from HOME → hbmenu in **application mode**: hold-R + A on the chosen installed title
      (using `/input/hold` R → tap A → wait for hbmenu → `/input/release` R). Also record the Album path (applet mode). It's only used
      to test the app's "can't run from the Album yet" screen (D21) → plus A on the user picker; `tests/e2e/device/profile.json`, D24
- [x] Launch the demo both ways: (a) hbmenu netloader **Y** + `nxlink -a <ip>` from WSL, (b) select it in hbmenu.
      Record which works and how long it takes → both work. (a) ~2 s incl. sending 4.5 MB, preferred (D24). (b) 1.2 s after A, position-dependent
- [x] Tap buttons in the demo, take a screenshot (save to `tests/e2e/out/wp00/`), quit with PLUS, check we're back in hbmenu
      → stock demo has PLUS-quit off; the spike build turns it on (one line). Back in hbmenu ~1.3 s
- [x] **Player-1 check**: does hbmenu/demo react to the virtual controller with Joy-Cons attached? With them detached? → yes, both (full loop passes)
- [x] **Touch check** (handheld): the demo reacts to `/input/touch` taps and `/input/swipe` drags from sys-autopilot-nx, the same way it reacts to real touches. Screenshot each
      → tap opened Pokedex, swipe scrolled it (`switch-8-touch.jpg`, `switch-9-swipe.jpg`)
- [x] **Mode + memory probe**: a tiny test .nro that writes `appletGetAppletType()` and `svcGetInfo(InfoType_TotalMemorySize / UsedMemorySize)`
      to SD as JSON. Run it in application mode and record the numbers in STATUS.md. These set the `Full` budgets (D12, D21).
      Run it once from the Album too, only to confirm it reports an applet type (expect `LibraryApplet` = 2), which is what the app's
      applet-mode guard checks. Applet memory numbers aren't needed now (WP-21)
      → `tools/spike/probe/`. Application: type 0, heap 3,184 MiB. Album: type 2. "Used" counts the whole heap → D12 amended (owner OK)
- [x] In application mode, also check: does the software keyboard open? What happens to the app on HOME and when coming back?
      Record findings. (The same checks for applet mode move to WP-21.) → keyboard opens and returns text (default limit 32 chars, PLAN risks).
      HOME → HOME menu ("Playing"), HOME again → app back with its state
- [x] Crash folders: list `atmosphere/crash_reports` and `atmosphere/fatal_errors` (just learn the paths/format)
      → `<time>_<programid>.log` (+ `.jpg`, + `dumps/<same>_thread_info.bin`), text "Atmosphère Crash Report (v1.7)". No new files in WP-00
- [x] Throwaway scripts kept in `tools/spike/` (WP-03 turns them into the real driver) → `lib.sh`, `wp00-loop.sh`, `build-demo.sh`, `build-probe.sh`
- [x] STATUS.md "Console facts" all filled in. Any surprise added to DECISIONS.md or PLAN.md risks → D12 amendment, D24, 3 PLAN rows

## Acceptance
One command sequence the owner watches: build → deploy → launch → 3 button presses → 1 touch tap → screenshot saved →
log file read back → app exited. It works twice in a row without anyone touching the console, **in application mode** (D21).

## Sonnet-safe chunks
None. This is exploratory and hardware-dependent.

## Gotchas
- WSL2 on Win10 is NAT'd. Outbound WSL→Switch works. Switch→WSL (nxlink `-s` stdio server) probably doesn't, so read logs from SD instead.
- If `.local` doesn't resolve in WSL, use the reserved IP.
- Don't install anything on the console beyond sys-autopilot-nx without asking the owner.
- Our crash handler doesn't exist yet (WP-03b). Until it does, a CPU crash in applet mode becomes a reboot screen (SAFETY.md).
  The stock borealis demo is stable, but run any *modified* test build in application mode first. The only thing that runs in applet mode
  in this WP is the tiny mode probe.
- Only ever write to `sdmc:/switch/suwayomi-nx/` and `sdmc:/switch/` (the demo .nro) through sys-autopilot. Never into `atmosphere/`, `bootloader/` or `config/`
  (the one exception: `config/sys-autopilot/config.ini`, which the owner edits).
