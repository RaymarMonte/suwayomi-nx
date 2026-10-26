# Status (handoff note, overwrite each session, ≤ 40 lines)
**Phase 1 – Core (WP-04 … WP-07) is done. Next WP:** WP-08 (app shell, settings, logging, server setup/login). Done: WP-00a … 07.

## Last session (2026-10-26, WP-07, Opus) — done
- **image now has (D39, `src/image/README.md`):** `ImageLoader::load(LoadRequest{url, box, priority, kind}, cancel, done)` →
  `done(ImageResult)` on the main thread (`ImagePtr` = RGBA + tiles ≤ 2048 px, or `ImageError`); one download per URL; page and
  cover memory caches; disk cache `cache/images/` (write-once, checksummed, budget, free-space guard); `TextureBudget` (S7).
- **Decoders:** libjpeg-turbo (DCT scaling), libpng, libwebp 1.6.0 from the new submodule `library/libwebp` (devkitPro's 1.0.2
  has CVE-2023-4863), our own GIF decoder. Header guard: sides ≤ 16384, one decode ≤ `maxDecodeBytes` (core `planDecode`).
- **Switch benchmark** (`tools/e2e.sh switch image-bench`, v1, app mode, from the SD cache): JPEG 1000×1500 page 18 ms (was 134 ms
  before the new `shrinkSlackPercent` 15: no 4 % CPU shrink), WebP strip 800×12000 176 ms, SD read 6–11 ms, peak +38.6 MB.
- **Fixed on the way:** (1) **safety gate** `check-elf.sh` compared hex words as numbers (`903e2100` = infinity): 236 false
  "stored pointers" once `appletSetCpuBoostMode` moved. Now text compares; self-test still fails all 4 planted calls. **Owner:
  please OK this gate change** (CLAUDE.md). (2) Desktop `runtimeInfo()` counts glibc's mmap'd blocks (images were invisible).
- **For WP-08+:** main() already has the `ImageLoader` (shutdown before http); screens `load()` with their token, `cached()` to
  draw at once; idle check can add `activeJobs()`. Reader: box 2× screen, webtoon `{2× width, 0}` + `tileWindow()`.
- **Green:** unit 6/6, test-hooks, TSan, one-off ASan/UBSan (170/171: `mallinfo2()` reads 0 under ASan; no memory errors),
  coverage (`src/image` 89.5 %), fuzz (3 targets), contract, integration ×3 (+ loader on real pages), desktop + smoke, e2e
  `image-bench` desktop + Switch, Switch build (gate PASS, `.nro` 3.8 MB), `gate-selftest`, npm. CI: see the commit.
## Known facts about the environment
- Windows 10 19044, 32 GB RAM. WSL Ubuntu 24.04 + WSLg; devkitPro + Node 22 in WSL. Run WSL commands with
  `wsl.exe -d Ubuntu --exec bash -lc '…'` (complex quoting: a script file + `bash -l <file>`; from Git Bash set
  `MSYS_NO_PATHCONV=1`, or `/mnt/c/...` arguments get rewritten). Git Bash has no python. The Bash tool turns `\\` in a heredoc
  into `\`: write files with backslashes (regexes, CMake escapes) with the Write/Edit tools.
- Builds (from `/mnt/c`): unit tests ~10 s, desktop ~50 s, Switch ~55 s clean (+ gate ~5 s); `.nro` 2.7 MB. 0 warnings in our code.
  Backups (WP-00a): `C:\Users\JohnDoe\SwitchBackup\2026-10-22\`. sysMMC NAND: owner's external drive.
- Test server: `reset` + `start` ~10 s; `tools/record-fixtures/record-all.sh` ~1 min (it ends with the server running, auth none).
- WSL can fail to start ("insufficient system resources") when Windows memory is nearly all committed (it reserves up to 12 GB): ask the owner to close apps.
- Device: e2e `net-hello` ~35 s, `smoke` ~25 s, `crash-drills` ~9 min (one drill ~50 s). A start-up crash leaves no log: use breadcrumb writes (WP-03c).
- Owner's real server (D18 "Update", never in tests): the Switch uses `http://<server-lan-ip>:4568`, no auth. Addresses: `docs/local/`.
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
