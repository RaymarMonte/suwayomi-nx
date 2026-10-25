# Status (handoff note, overwrite each session, ≤ 40 lines)

**Current phase:** 0 – Foundations
**Next WP:** WP-03 E2E harness (Opus). WP-00a, WP-00b, WP-00, WP-01 and WP-02 are done.

## Last session (2026-10-25, WP-02, Opus) — done
- **Acceptance met:** `tools/test-server/test-server.sh reset` + `start` = **~8–10 s** (Java answers in ~4.5 s, then `seed.ts`) with the fixture
  library: 6 manga, categories Reading / Long series. Fixtures: 665 files, 16 MB, same digest every run (Linux disk, Windows disk, test).
  Schema (363 types) + 17 recorded answers committed; re-recording on a fresh server gives identical files. ❔ list in SUWAYOMI-API.md resolved.
  Reachable from WSL (`http://<windows-host>:4568`, the default gateway), via the PC's LAN address, and from the owner's phone.
  `npm test` 14/14 (page code read back from a scaled 1280×720 JPEG screenshot), `npm run typecheck` clean, `tools/build.sh tests` green.
- **Decisions:** D29 test server (pinned v2.4.2366, files in `<repo>/.test-server/`, Java started via WMI), D30 Node tooling (root npm package,
  not pnpm; Node runs `.ts` directly; new CI job `node`), D31 fixture pages + colour code. WP-03's line about a pnpm package now points to D30.
- **Found:** Claude Code's sandbox hides files that Windows programs write outside the project folder from WSL (hence `.test-server/` in the repo).
  Firewall: the PC's Ethernet is classed Public; the owner added a TCP 4568 LocalSubnet rule (all profiles) and switched off two old
  "OpenJDK Platform binary" Public **Block** rules for jdk-21.0.11 (SETUP.md A.4). After a Java update, check for a new Block rule.
- **For WP-04/05/06 (SUWAYOMI-API.md):** `pageCount` is -1 until `fetchChapterPages` ran, and progress sent before that is saved as 0; the cap is
  `pageCount`. Fields of one request run concurrently. ui_login without a token = HTTP 200 + "Unauthorized" GraphQL error; basic = HTTP 401.
  Error messages contain a Java stack trace after the first line. `category(id: 0)` = manga in no category. Page URLs use the chapter's sourceOrder.
- CI's new `node` job (also checks fixture determinism on another machine) runs on the next push. CLAUDE.md's "pause at 92 %" rule is committed too.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM, 24 threads. PC on Ethernet, DHCP, network classed Public (LAN address: `test-server.sh status`). WSL 3.0.3 + WSLg, Ubuntu 24.04. Java 21, git. devkitPro + Node 22 in WSL (SETUP.md A). Run WSL commands
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
- A DHCP reservation for the PC (like the Switch has) would keep the test-server URL stable for device tests (WP-05). Owner's choice.
Answered 2026-10-25: personal details in docs (D23), commit email (noreply), banned library functions (D28), publish repo: yes.
## Escalation
(none)
