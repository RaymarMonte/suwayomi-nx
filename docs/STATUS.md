# Status (handoff note, overwrite each session, ≤ 40 lines)

**Current phase:** 0 – Foundations
**Next WP:** WP-03b Safety layer (Opus). Its crash drills need the console. WP-00a, WP-00b, WP-00, WP-01, WP-02 and WP-03 are done.

## Last session (2026-10-25, WP-03, Opus) — done
- **Acceptance met:** `tools/e2e.sh desktop smoke` green many times (~1 s), `tools/e2e.sh switch smoke` green twice in a row (14–22 s:
  HOME → hold R + A → hbmenu → nxlink → app → golden → PLUS → crash folders → back to HOME). `app_start` = application/full on both
  (Switch heap 3,336,155,136 B). Desktop-only `harness.test.ts`: applet flag, `--mem-limit`, a killed app fails in < 1 s. A painted golden
  fails with `hello.diff.png`. C++ unit tests, 96 Node tests, typecheck green. How to write a scenario: `tests/e2e/README.md`.
- **Decisions:** D32 (harness design). Owner OK: `atmosphere/fatal_reports/` joins the S10 read list and the fatal gate (SAFETY.md, TESTING.md).
- **For WP-03b:** test events and the log file are stop-gap `fopen` writes (`src/app/TestHooks.cpp`, `main.cpp` `openLogFile`) that move to
  SafeFs. `--mem-limit`/`runtimeInfo()` exist now. Crash-drill scenarios run through `tools/e2e.sh switch`; the Switch driver already diffs the
  crash folders and stops hard on a new fatal file (`tests/e2e/out/STOP-console-fatal.txt`). `APPLET_MODE_READY` in `SwitchDriver.ts` stays
  false until WP-08. CI still runs `tools/desktop-smoke.sh`; a desktop-E2E CI job is a later chore.
- **Found:** (a) the console resets new connections for ~10 s after bursts (~50/s), curl's too: the harness sends one request at a time,
  ≥ 150 ms apart. (b) Horizon FS: a file open for writing can't be opened by another program, so events go through a pending queue and
  `app.log` is readable only after the app quits. (c) Atmosphère's fatal screen writes `fatal_reports/` (not when creport caused it).
  Not checked: `--visible` (WSLg) and "app closed mid-test" on the console (the 15 s event timeout and the 3 s screen check cover it).
## Known facts about the environment
- Windows 10 19044, 32 GB RAM, 24 threads. PC on Ethernet, DHCP, network classed Public (LAN address: `test-server.sh status`). WSL 3.0.3 + WSLg,
  Ubuntu 24.04. Java 21, git. devkitPro + Node 22 in WSL (SETUP.md A). Run WSL commands with `wsl.exe -d Ubuntu --exec bash -lc '…'`
  (not `--`); from Git Bash set `MSYS_NO_PATHCONV=1`. Under WSLg `/tmp/.X11-unix` is read-only (Xvfb uses its abstract socket).
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean; `.nro` 2.5 MB, `.elf` 52 MB (debug info), 0 warnings in our code.
  Router: DHCP reservation for the Switch (SETUP.md B.6); WSL reaches it by IP. Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`
  (`sd\` + `hekate-backup\` emuMMC, SHA-256 checked). sysMMC NAND: owner's external drive.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC partition-based** (`emuMMC/RAW1`). Boots by **RCM payload**. Hekate **6.5.3**, autoboot off;
  Restart from HOME → Hekate → Launch → **CFW (EMUMMC)**. SD over USB: Hekate → Tools → USB Tools → SD Card = "SWITCH SD" (FAT32); never format the 2nd drive.
- SD: **FAT32**, 64 KB clusters, 189.6 GB partition (~88 GB free). `exosphere.ini`: blank_prodinfo_emummc=1. DNS mitm on. hbmenu 3.6.1, nx-hbloader 2.4.5.
- Sysmodules at boot: sys-clk, emuiibo, MissionControl, sys-patch (exits), nx-ovlloader + **sys-autopilot-nx `4200000000004150`** (`log = false`).
- **sys-autopilot-nx:** `http://<switch-ip>:4150`. Heap 304,768 B idle. Screenshot ~190 KB in 0.1–0.7 s (fails during app switches: retry).
- Baseline: `fatal_errors` empty, `fatal_reports` only `dumps/`, `crash_reports` 17 files + 10 in `dumps/`. Console clock is off: compare lists, not dates.
- Auto-Sleep: Never. Airplane mode must be off. **App mode:** Spelunky 2 must be the **first HOME tile**; hold R + A, then A on "Who is using
  the software?", hbmenu ~3.7 s later. Album → hbmenu shows a red "Applet Mode" badge; HOME leaves it (PLUS doesn't).
- **Memory:** application mode heap ~3,181–3,184 MiB. Album: appletType 2, heap 266 MiB (WP-21 only).
## Open questions for the owner
- A DHCP reservation for the PC (like the Switch has) would keep the test-server URL stable for device tests (WP-05). Owner's choice.
## Escalation
(none)
