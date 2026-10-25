# Status (handoff note, overwrite each session, ≤ 40 lines)

**Current phase:** 0 – Foundations
**Next WP:** finish WP-03 (paused, see below). WP-00a, WP-00b, WP-00, WP-01 and WP-02 are done.

## Paused (2026-10-25, WP-03, Opus) at the 5-hour usage limit (resets 11:10 UTC). Last test runs all green, committed as WIP.
- **Done, green:** app test hooks (`src/app/TestHooks`, events queued and appended per frame), `core/LaunchOptions` (`--simulate-applet-mode`,
  `--mem-limit=`), `core/RuntimeInfo` + `platform::runtimeInfo()` (Switch: appletType, heap, mallinfo), SIGTERM = clean quit on desktop.
  Harness: `tests/e2e/drivers/` (Driver, DesktopDriver, SwitchDriver, autopilot.ts with the S10 allowlist, screencheck.ts), `lib/events.ts`,
  `lib/golden.ts` (pixelmatch 7.2.0 + sharp), `lib/scenario.ts`, `scenarios/smoke.test.ts` + desktop-only `harness.test.ts`, goldens for both
  drivers, `tools/e2e.sh`, `npm run e2e`. **Acceptance met:** desktop smoke green many times (~1 s); `tools/e2e.sh switch smoke` green twice in a
  row (14–22 s); applet flag checked (`harness-applet-flag`); a painted golden fails with `hello.diff.png`; a killed app fails in < 1 s.
  C++ unit tests, 96 Node tests, typecheck green. Owner OK: `atmosphere/fatal_reports/` joins the S10 read list as a second hard-stop folder.
- **Exact next steps:** (1) `tests/e2e/README.md` (how to write a scenario; Sonnet-safe). (2) TESTING.md: Driver interface (PNG desktop /
  JPEG Switch, `dispose()`, desktop keys only A=Enter B=Esc X=middle mouse + D-pad, quit = SIGTERM), fatal_reports, events written
  open-append-close with a pending queue, request pacing. (3) SAFETY.md S10 + fatal gate: add `fatal_reports`. (4) CLAUDE.md "E2E desktop /
  device" command line. (5) DECISIONS D32 harness design (sharp instead of jpeg-js/pngjs; own HTTP client; pacing; `APPLET_MODE_READY` flag in
  SwitchDriver for WP-08). (6) core/platform READMEs list the new headers. (7) Tick WP-03 checklist, ROADMAP status, commit `WP-03: ...`.
  Optional: check "app closed mid-test" once on the Switch; `--visible` (WSLg) is untested.
- **Found (for D32/TESTING):** (a) the console resets new connections for ~10 s after bursts (~50/s), curl's too. Fix: one request at a time,
  ≥ 150 ms apart, the PC closes each connection, reads back off up to 8 s; inputs never retried. (b) Horizon FS: a file open for writing can't
  be read by sys-autopilot and vice versa, so the app's events go through a pending queue (an `idle` got lost before), and `app.log` is read
  only after the app quits. (c) Node fetch/http reported resets that a plain socket didn't; the client now speaks HTTP over `net` itself.
  (d) Atmosphère's fatal screen writes `atmosphere/fatal_reports/` (not when creport caused it); `fatal_errors/` is written at the next boot.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM, 24 threads. PC on Ethernet, DHCP, network classed Public (LAN address: `test-server.sh status`). WSL 3.0.3 + WSLg, Ubuntu 24.04. Java 21, git. devkitPro + Node 22 in WSL (SETUP.md A). Run WSL commands
  with `wsl.exe -d Ubuntu --exec bash -lc '…'` (not `--`); from Git Bash set `MSYS_NO_PATHCONV=1`. Under WSLg `/tmp/.X11-unix` is read-only (Xvfb uses its abstract socket).
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean; `.nro` 2.5 MB, `.elf` 52 MB (debug info), 0 warnings in our code.
  Router: DHCP reservation for the Switch (SETUP.md B.6); WSL reaches it by IP (`.local` 10× slower). Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\` (`sd\` + `hekate-backup\` emuMMC, SHA-256 checked). sysMMC NAND: owner's external drive.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC partition-based** (`emuMMC/RAW1`). Boots by **RCM payload**. Hekate **6.5.3**, autoboot off;
  Restart from HOME → Hekate → Launch → **CFW (EMUMMC)**. SD over USB: Hekate → Tools → USB Tools → SD Card = "SWITCH SD" (FAT32); never format the 2nd drive.
- SD: **FAT32**, 64 KB clusters, 189.6 GB partition (~88 GB free). `exosphere.ini`: blank_prodinfo_emummc=1. DNS mitm on. hbmenu 3.6.1, nx-hbloader 2.4.5.
- Sysmodules at boot: sys-clk, emuiibo, MissionControl, sys-patch (exits), nx-ovlloader + **sys-autopilot-nx `4200000000004150`** (`log = false`).
- **sys-autopilot-nx:** `http://<switch-ip>:4150` (DHCP reservation). Heap 304,768 B idle. Screenshot ~190 KB in 0.1–0.7 s (fails during app switches: retry).
- Baseline: `fatal_errors` empty, `fatal_reports` only `dumps/`, `crash_reports` 17 files + 10 in `dumps/`. Console clock is off: compare lists, not dates.
- Auto-Sleep: Never. Airplane mode must be off. **App mode:** Spelunky 2 must be the **first HOME tile**; hold R + A, then A on "Who is using
  the software?", hbmenu ~3.7 s later. Album → hbmenu shows a red "Applet Mode" badge; HOME leaves it (PLUS doesn't).
- **Memory:** application mode: heap **3,181–3,184 MiB** (app_start memTotal 3,336,155,136). Album: appletType 2, heap 266 MiB (WP-21 only).
## Open questions for the owner
- A DHCP reservation for the PC (like the Switch has) would keep the test-server URL stable for device tests (WP-05). Owner's choice.
## Escalation
(none)
