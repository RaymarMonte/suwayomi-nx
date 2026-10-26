# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 1 – Core (WP-04 … WP-07). Next WP:** WP-05 (net), **paused half-way** (see below). Then WP-06 (needs 04 + 05). Done: WP-00a … WP-04.

## Paused (2026-10-26, WP-05, Opus, at the 5-hour usage limit; it resets 03:00 UTC)
- **Done, green (unit 6/6, contract 26/26, 0 warnings), committed locally as `WP-05: WIP, paused at usage limit`, NOT pushed**
  (CI's `tests`/`coverage` jobs don't install `libcurl4-openssl-dev` yet, so a push would go red):
  `net/HttpClient.hpp` (Request/Response/Priority/hooks), `CancelToken.hpp`, `Future.hpp` (then() + wait; D37 still to write),
  `CurlHttpClient` (one thread + curl multi, priorities, cancel ≤ 50 ms, body cap while streaming + Content-Length check, header cap,
  gzip, http/https-only redirects, retries, resetConnections, shutdown, pthread stack size), `FakeHttpClient`, `core/RetryPolicy`,
  new profile fields (`httpConnectTimeoutMs` … `netThreadStackBytes`), `ErrorKind` Network/Timeout/Cancelled/TooLarge,
  cpp-httplib v0.60.1 in `third_party/httplib/` (host tests only), tests `test_net.cpp` + `test_net_curl.cpp` (9 curl cases).
- **Next steps, in order:** (1) `tools/build.sh tsan` mode (`-DSNX_TSAN=ON` → `-fsanitize=thread`) and run it; (2) add
  `libcurl4-openssl-dev` to CI apt lines (tests, coverage, desktop, fuzz jobs) + a CI tsan step; (3) `third_party/README.md` row for
  httplib (sha256 `e932d7771bdb93150868987e30112d9a31e24bef592d6d7a07e973d5845cf8d6`, MIT) + `CREDITS.md`; (4) `src/net/README.md`;
  (5) `tools/build.sh desktop` + `switch` (Switch links `curl` + `z` from portlibs; switch-curl is 7.69, code guards for it; gate must
  PASS); (6) DECISIONS D37 (Future with then(), callbacks on the net thread, api wraps with brls::sync; errors as `Result<Response>`;
  retry only GET + transient; profile holds net limits); (7) Switch item: find borealis's socket config (`switch_wrapper.c`), Hello app
  fetches `aboutServer` + 20-parallel burst — **ask the owner before using the Switch** (they asked); (8) tick WP checklist, STATUS,
  commit `WP-05: …`, push, `gh run watch`.
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
