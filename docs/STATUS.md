# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 1 – Core (WP-04 … WP-07). Next WP:** WP-07 (image pipeline + caches, needs 05). Done: WP-00a … WP-06.

## Last session (2026-10-26, WP-06, Opus) — done
- **api now has (D38, `src/api/README.md`):** `SuwayomiClient(http, settings, clientOptionsFrom(profile))` with serverInfo,
  categories, library(categoryId, page), manga, chapters(mangaId, page), pages(chapterId, PageFormat), setProgress, login, logout,
  configure, settings, absoluteUrl. Every call: CancelToken → `ApiFuture<T>` = `Future<ApiResult<T>>`, `ApiError{kind, message,
  detail}` (Offline/Timeout/Auth/Permission/Server/Parse/Cancelled). URL helpers in `ServerUrl.hpp`; `serverWarning()` for D9.
  core's `Result<T, E>` gained an error-type parameter (default unchanged).
- **Login:** basic header on everything; ui_login Bearer, renewal on refusal: refreshToken → login with saved password → Auth.
  Single flight (5 parallel refusals → 1 refresh), refusals remembered until the login changes. Images get the header from the
  decorator; an image 401 starts a background renewal. No early renewal by `exp` (token has no `iat`, the Switch clock is off).
- **For WP-07 (image):** send image requests through the same `HttpClient` the app's SuwayomiClient decorates (auth is added for
  you); full URL = `client.absoluteUrl(path)`; on HTTP 401 under ui_login try once more after a moment (the renewal is under way).
- **For WP-08:** one SuwayomiClient in main() after the HttpClient; `deliver` = `brls::sync`, `onSettingsChanged` → save config.
- **Green:** unit 6/6 (api 34 cases, 97 % lines), test-hooks build, ThreadSanitizer (single-flight on real threads, 30 repeat runs),
  contract 26/26, coverage, new `tools/build.sh integration` in none/basic/ui_login (~64 s, incl. the 20 s expiry wait), desktop
  build + smoke, Switch build (gate PASS). No Node or UI changes, so no npm/e2e/device runs.
- **Facts:** a re-seeded test server shows Alpha's status UNKNOWN (first seeding: ONGOING). Integration isn't in CI (Linux server).
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 2.7 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB):
  ask the owner to close apps. Device e2e: `net-hello` ~35 s, `smoke` ~25 s.
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
