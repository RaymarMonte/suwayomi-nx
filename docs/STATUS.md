# Status (handoff note, overwrite each session, ≤ 40 lines)

**Current phase:** 0 – Foundations
**Next WP:** WP-02 test server + fixtures (Opus). WP-00a, WP-00b, WP-00 and WP-01 are done.

## Last session (2026-10-25, WP-01, Opus) — done
- **Acceptance met:** `tools/build.sh tests` green (5 ctest entries, one per module; also with `--test-hooks`). Desktop app opens in WSLg
  (seen by the owner) and passes `tools/desktop-smoke.sh` on Xvfb. `.nro` shows Hello on the console: `tools/spike/wp01-hello.sh`
  passed twice (~24 s; app mode → nxlink → SHA-256 → screenshot → PLUS → log → no new crash/fatal files; reference crop
  `tests/e2e/device/local/hello-header.png` cut from run 1, checked by eye). Coverage 90% lines.
- Repo: public `RaymarMonte/suwayomi-nx`, noreply commits (repo-local git config). `gh` 2.103.2 in WSL, logged in: push from WSL (SETUP.md C).
  CI: `.github/workflows/ci.yml`, jobs host / desktop / switch (D27). Docs write `<switch-ip>` and `JohnDoe` (user name) since D23.
- **Decisions:** D25 layout + borealis pin `5f08b28` (only its glfw submodule), D26 two nlohmann/json copies kept apart, D27 CI,
  D28 (owner OK) banned functions pulled in by libraries get `ld --wrap` do-nothing stand-ins → **WP-03b builds them**. D23 answered.
- Safety baseline: `docs/safety-baseline.txt`. Two S3 hits, both never called: libnx `appletInitialize` → `apmSetPerformanceConfiguration`
  (only for a non-zero `__nx_applet_PerformanceConfiguration`, default zeros), borealis's virtual `setBacklightBrightness` → `lblSet…`.
- **For WP-03/03b:** `main.cpp` opens `<data>/logs/app.log` with `mkdir` + `fopen` (flag `--log-file`) as a stop-gap: replace with the
  logger + SafeFs (S4 grep would flag it). Desktop borealis maps no key to PLUS (only Enter/Esc/arrows): the desktop driver needs a test
  hook to quit. GLFW picks WSLg's Wayland even without `WAYLAND_DISPLAY`; headless runs need an empty `XDG_RUNTIME_DIR` (smoke script does it).
  borealis aborts (libdbus assertion) when there's no D-Bus session bus: CI installs `dbus-x11` so a bus starts on demand.

## Known facts about the environment
- Windows 10 19044, 32 GB RAM, 24 threads. WSL 3.0.3 + WSLg, Ubuntu 24.04. Java 21, git. devkitPro + Node 22 in WSL (SETUP.md A). Run WSL commands
  with `wsl.exe -d Ubuntu --exec bash -lc '…'` (not `--`); from Git Bash set `MSYS_NO_PATHCONV=1`. Rancher Desktop's Docker daemon was not responding.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean; `.nro` 2.5 MB, `.elf` 52 MB (debug info), 0 warnings in our code.
  Router: DHCP reservation for the Switch (SETUP.md B.6); WSL reaches it by IP (`.local` 10× slower). Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\` (`sd\` + `hekate-backup\` emuMMC, SHA-256 checked). sysMMC NAND: owner's external drive.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC partition-based** (`emuMMC/RAW1`). Boots by **RCM payload**. Hekate **6.5.3**, autoboot off;
  Restart from HOME → Hekate → Launch → **CFW (EMUMMC)**. SD over USB: Hekate → Tools → USB Tools → SD Card = "SWITCH SD" (FAT32); never format the 2nd drive.
- SD: **FAT32**, 64 KB clusters, 189.6 GB partition (~88 GB free). `exosphere.ini`: blank_prodinfo_emummc=1. DNS mitm on. hbmenu 3.6.1, nx-hbloader 2.4.5.
- Sysmodules at boot: sys-clk, emuiibo, MissionControl, sys-patch (exits), nx-ovlloader + **sys-autopilot-nx `4200000000004150`** (`log = false`).
- **sys-autopilot-nx:** `http://<switch-ip>:4150` (DHCP reservation). Heap 304,768 B idle. Screenshot ~190 KB in 0.1–0.7 s (fails during app switches: retry).
- Baseline: `fatal_errors` empty, `crash_reports` 17 files + 10 in `dumps/` (unchanged after WP-01). Console clock is off: compare lists, not dates.
- Auto-Sleep: Never. Airplane mode must be off. **App mode:** Spelunky 2 must be the **first HOME tile**; hold R + A, then A on "Who is using
  the software?", hbmenu ~3.7 s later. Album → hbmenu shows a red "Applet Mode" badge; HOME leaves it (PLUS doesn't).
- **Memory (probe):** application mode: heap **3,184 MiB**. Album: appletType 2, heap 266 MiB (WP-21 only).
## Open questions for the owner
(none) · Answered 2026-10-25: personal details in docs (D23), commit email (noreply), banned library functions (D28), publish repo: yes.
## Escalation
(none)
