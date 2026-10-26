# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 1 – Core (WP-04 … WP-07). Next WP:** WP-06 (api: SuwayomiClient, needs 04 + 05). Done: WP-00a … WP-05.

## Last session (2026-10-26, WP-05, Opus; paused once at the usage limit, owner present at the end) — done
- **net now has (D37, `src/net/README.md`):** `HttpClient` + `Request`/`Response`/`Priority`, `CancelToken`, `Future` with `then()`,
  `CurlHttpClient` (one thread + curl multi, ≤ 6 transfers, priority queue, cancel ≤ 50 ms, body cap while streaming, gzip,
  http/https-only redirects, retries for GETs and `idempotent` POSTs, `resetConnections`, `shutdown`), `FakeHttpClient`.
  core: `RetryPolicy`, `ErrorKind` Network/Timeout/Cancelled/TooLarge + `errorKindName`, net limits in `RuntimeProfile`, test config
  `server.url`, `net` test event. `main()` owns the client; the Hello screen shows the server version + a 20-request burst.
- **For WP-06:** wrap `then()` with `brls::sync`; mark GraphQL queries `idempotent = true` (never mutations); POST to
  `{url}/api/graphql`; read limits from the profile. The Hello server check is temporary (WP-08 replaces it).
- **Green:** unit 6/6 (+ under ThreadSanitizer: new `tools/build.sh tsan`, also a CI step), contract 26/26, net coverage 95 %,
  npm test 105, typecheck, desktop + Switch builds (gate PASS with curl linked), e2e `smoke` + new `net-hello` on desktop and Switch.
  CI green on `ba8807e` (5 jobs, tsan step included).
- **Fixed:** `tests/e2e/lib/suwayomi.ts` `windowsHost()` reversed the IP (1.64.27.172); now `gatewayFromRoute()` with a test.
- **Facts:** the Switch's curl is 7.69 with the blocking DNS resolver (use IPs); borealis's socket setup (12 BSD sessions) is kept.
  `appServerUrl()` finds the PC's LAN address from `test-server.sh status`, so no DHCP reservation is needed.
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
