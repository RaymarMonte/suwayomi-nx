# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 3 – Features. Now:** **WP-17 planned** (download on server, D58, `docs/wp/WP-17-download-on-server.md`).
**Next: run WP-17** (Opus, M). Order (D57): WP-17 → 18 → 18b → 21 → 16 → 25 → 19; nice to have: 08b, 26, 27, 28.
Done: WP-00a … 13, 13b, 13c, 13d, **14, 14b, 15, 15b, 23, 24**, 22; **v0.2.0** (13…24 unreleased). WP-13d: the console's
longest strip frame is 31–32 ms (limit 34).
## Last session (2026-11-19, WP-17 planning, Opus) – plan committed, no code
- Wrote the WP-17 doc, D58, the ROADMAP row and stubs (WP-18's too). Owner's answers: the queue on a **"Server downloads"
  tab** (WP-18 gets its own "Downloads" tab), the details screen like Suwayomi's web UI (Next 1 / 5 / 10 / 25, Unread, All +
  a "Downloaded on server" filter), Move to top / bottom only, a Settings switch "Delete server downloads after reading",
  and **Opus** for it (it grew to size M; D57 had allowed Sonnet).
- Checked on the test server (facts in the WP doc; they go into SUWAYOMI-API.md during the WP): finished chapters leave the
  queue at once, an enqueue answers STOPPED and starts ~1 s later, `startDownloader` with nothing queued and removing a
  chapter no longer queued each hang 30 s, ERROR rows stay until enqueued again, Local chapters are dropped from the queue.
- The test server is left running (no auth); the probes browsed SNX Test (rows from manga 158 / chapter 462 on) and left
  server downloads on its disk (queue empty, no switch file): a `reset` wipes it. Before: WP-14b done. The console sleeps.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Windows JDK: Temurin 21 (`C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot\bin`: java, javac, jar, jwebserver); WSL has none.
- Builds: unit tests ~10 s, desktop ~50 s, Switch ~55 s (+ gate ~5 s), 0 warnings. Backups: `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`.
- Test server: `reset` + `start` ~10 s, the test site (4569) with it (desktop e2e needs it running); won't start ("null cannot be cast"): `reset`, start again.
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `smoke` ~30 s, `net-hello` ~25 s, `manga-details` ~67 s, `reader-paged` ~120 s, `resume-progress` ~50–80 s,
  `reader-rtl-spread` ~40 s, `broken-images` ~36 s, `auth-matrix` ~110 s, `crash-drills` ~9 min (one drill ~50 s),
  `reader-webtoon` ~7 min, `reader-zoom` ~7 min, `reader-portrait` ~2.4 min, `reader-spread-split` ~1.4 min, `updates-history` ~4.7 min, `library-browse` ~2.4 min, `offline-errors` ~2.7 min.
  The desktop text dialog ignores Enter: tap its OK. A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
## Console facts
- **v1 Erista · FW 22.5.0 · AMS 1.11.2 · emuMMC** (`emuMMC/RAW1`), RCM payload, Hekate 6.5.3 (Launch → CFW (EMUMMC)). SD FAT32, ~88 GB free.
- Sysmodules: sys-clk, emuiibo, MissionControl, sys-patch, nx-ovlloader, **sys-autopilot-nx `4200000000004150`** (`http://<switch-ip>:4150`).
- Baseline (2026-10-30): `fatal_errors` empty, `fatal_reports` 3 files + `dumps/` (qlaunch fatals from the Album: WP-08's, D41, and
  two dated Oct 29, before WP-12), `crash_reports` 26 files + `dumps/`.. Clock is off.
- **App mode:** Spelunky 2 = first HOME tile (playing another game moves it: the owner opens and closes Spelunky 2 again); hold R + A, A on the user picker, hbmenu ~3.7 s later. Heap ~3,181–3,184 MiB.
- Console resets new connections after bursts (~50/s): one request at a time, ≥ 150 ms apart. A file open for writing can't be
  read by another program (events use a pending queue; `app.log` readable after quit). HOME on the HOME menu resumes the app.
- Harness: the console resets single new connections and spells of them (10–30 s+); requests retry ~60 s, inputs only if the console never had them (D32).
## Escalation
(none)
