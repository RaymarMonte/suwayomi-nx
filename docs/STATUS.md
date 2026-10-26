# Status (handoff note, overwrite each session, ≤ 40 lines)

**Current phase:** 1 – Core (no UI), WP-04 … WP-07
**Next WP:** WP-05 (net: HttpClient, no console). Then WP-06 (needs 04 + 05). Done: WP-00a … WP-04.

## Last session (2026-10-26, WP-04, Opus, no owner or console needed) — done
- **core now has:** `Result<T>` errors-as-values, `parseJson` with S6 limits (8 MiB, depth 64), tolerant models + `fromJson<T>`,
  9 GraphQL documents embedded at build time, `gql::` request builders + answer readers, `AppConfig` (schemaVersion + migration
  steps), `makeRuntimeProfile` (+ safe mode). All in D36 and `src/core/README.md`.
- **Found on the test server:** `category(id) { mangas }` can't be paged, so library tabs use `mangas(filter: {categoryId: …})`, with
  `{isNull: true}` for the Default category (SUWAYOMI-API.md). Our own documents are recorded too (`responses/app-*.json`, recorder
  `# document:` header); re-recording changed none of the WP-02 files.
- **Green:** unit 6/6 ctest entries (new `unit.core-includes` allowlist check), core coverage 94%, contract 26/26 (`npm run contract`,
  also at the end of `tools/build.sh tests`), npm test 103, typecheck, fuzz seed replay (GCC ASan/UBSan), Switch build (gate PASS).
  CI green on `f993b65` (5 jobs); its fuzz job: 1.16 M runs `fuzz_gql_response`, 0.43 M runs `fuzz_app_config`, no crash.
- **Fuzzing on the PC too:** the owner installed clang 18 + `libclang-rt-18-dev` in WSL; `tools/build.sh fuzz 30` ran 0.85 M / 0.29 M
  inputs, no crash. (Without clang the command falls back to replaying the seeds under GCC ASan/UBSan.)
## Open questions for the owner
- A DHCP reservation for the PC (like the Switch has) would keep the test-server URL stable for device tests (WP-05). Owner's choice.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 2.7 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
- Device: `crash-drills` ~9 min, one drill ~50 s. A start-up crash leaves no log: use temporary breadcrumb writes (WP-03c).
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline: `fatal_errors` empty, `fatal_reports` only `dumps/`, `crash_reports` 21 files + `dumps/` (4 from WP-03c). Clock is off.
- **App mode:** Spelunky 2 = first HOME tile; hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console sometimes resets new connections for 30 s+ around app start/exit; requests retry ~60 s, button presses don't.
## Escalation
(none)
