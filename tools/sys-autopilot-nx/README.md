# sys-autopilot-nx

Our slim fork of **sys-autopilot**, a Nintendo Switch sysmodule (a background system program) that lets the PC look at and control
the console over Wi-Fi: screenshots, controller buttons, **touch**, and SD card files, through a REST API and an MCP endpoint.
The suwayomi-nx test harness uses it to drive the app on a real console (see `docs/TESTING.md`, "SwitchDriver").
Why we have our own copy: decision **D19** in `docs/DECISIONS.md`.

| | |
|---|---|
| Upstream | https://github.com/TooTallNate/sys-autopilot by Nathan Rajlich, MIT (declared in its `package.json`; `LICENSE` here adds the text) |
| Copied at | commit `6ca6aac6722ff40be206118d3f93ade84234c762` (main, 2026-06-29 = v1.5.0 + HTTP keep-alive fix, mDNS NSEC fix) |
| How | plain copy (`git archive`), not a submodule. Upstream's README is kept as `UPSTREAM-README.md` |
| Our version | `1.5.0-nx.1` (in `package.json`; the Makefiles read it from there) |
| Title ID | `4200000000004150`, the same as upstream, so it replaces stock cleanly. Remove = delete that folder (see below) |

## What we changed and why

A sysmodule crash is a **system fatal** (the "press POWER to reboot" screen), and all sysmodules share one small memory pool.
Stock v1.5.0 has crash reports on FW 22 (upstream PR #30 at boot, issue #28 on `/screenshot`), uses ~6.7 MB of that pool, and can do
things a test console should never do. So:

**Removed** (the files are gone and the routes answer 404; their MCP tools are gone from `tools/list`):
- title installer (`/install`, `install.c`, `nx_ext.c`): it could write titles to internal NAND
- system settings (`/settings/*`: theme, nickname, brightness, volume via `audctl`, auto-time, clock) and airplane mode
- DNS changes (`/network/dns`), title list (`/titles`)
- power actions (`/power/sleep|restart|off`)
- OAuth login flow (`/oauth/*`, `/.well-known/*`, `tokens.txt`, `create_token`/`revoke_token`)
- upstream's release tooling (changesets, GitHub workflows, pnpm, NSZ installer script)

**Kept, unchanged in behaviour:** `/status`, `/screenshot`, `/input/tap|hold|release|stick|clear`, `/controller/attach|detach`,
`/files` (GET/PUT/DELETE) and `/files/hash`, the MCP endpoint `/mcp`, static bearer-token auth (and HTTP Basic), mDNS/DNS-SD
discovery, the optional file log, and the PSC sleep/wake handling (sockets and the virtual controller are released before sleep).

**Added:**
- `POST /input/touch` and `POST /input/swipe`, plus MCP tools `touch` and `swipe` (details below).
- `/status` also reports `"screenshots"` (true/false) and `"heap":{"size","peak","used"}` in bytes, to check the slim heap on the console.

**Safety changes:**
- **Fails closed:** with no token (and no username+password) in `config.ini`, every request gets 401. Upstream ran wide open then.
- **No new aborts.** Only sm, fs, hid:dbg and bsd can still abort at start-up (as upstream). `caps:sc` failing now means
  "no screenshots" (503) instead of a fatal. If PSC (sleep/wake) registration fails, the sysmodule opens no sockets and exits
  cleanly, because sockets without sleep handling crash the console at the next sleep. Upstream aborted in both cases.
- **NPDM service list 17 → 8:** `bsd:u caps:sc hid:dbg set:sys fsp-srv psc:m nifm:u psm`. Dropped: `time:s spsm spl: nifm:a lbl audctl ncm ns:am2 es`.
  `nifm` is opened read-only (`nifm:u`) now. The mDNS TXT record no longer has the Atmosphère version (that needed `spl:`).
- **Safety gate in `build.sh`:** the build fails if any dangerous system function is linked in (title install, settings
  setters, DNS/wireless, clocks, power, `fatalThrow`, …). Stock v1.5.0 links 22 of them; ours links 0.
- MCP `serverInfo.name` is `sys-autopilot-nx`. The mDNS service type stays `_sys-autopilot._tcp`.

## Memory (the point of the slim-down)

Sizes of `sys-autopilot.elf` from `aarch64-none-elf-size` (bytes). "Total" = text + data + bss = what the sysmodule takes from the pool
(plus its 32 KiB main thread stack). All numbers measured by `build.sh`.

| Build | text | data | bss | **total** |
|---|---:|---:|---:|---:|
| Stock upstream at the pinned commit | 361,291 | 15,776 | 6,640,744 | **7,017,811** (6.69 MiB) |
| 1. dangerous features removed | 232,907 | 14,360 | 4,968,368 | 5,215,635 (4.97 MiB) |
| 2. heap 4 MiB → 640 KiB, JSON parsers shared and halved | 233,163 | 14,360 | 1,380,240 | 1,627,763 (1.55 MiB) |
| 3. + touch/swipe | 239,099 | 14,408 | 1,380,240 | 1,633,747 (1.56 MiB) |
| 4. + heap peak in `/status` (= `1.5.0-nx.1` as installed) | 239,451 | 14,408 | 1,380,240 | **1,634,099** (1.56 MiB, −77 %) |

What the biggest buffers are and why they stay:
- **heap 640 KiB** (`source/main.c`): 296 KiB socket transfer memory (fixed by the socket settings; 304,768 bytes in use when idle,
  measured) + short-lived buffers. Measured peak: 458,672 bytes after MCP `read_file` of a 32 KiB log and of a binary file.
  Known limit: MCP `read_file` of 32 KiB of pure control bytes (each escapes to 6 bytes) answers "out of memory" (clean error, no crash).
  REST `GET /files` streams and needs no heap, so the harness never hits this. Upstream PR #30 used 512 KiB.
  Possible fix for the next rebuild: cap `read_file`'s escaped size instead of its raw size.
- **screenshot buffer 512 KiB**: libnx's `CAPSSC_JPEG_BUFFER_SIZE` for `capsscCaptureJpegScreenShot`. Not shrunk on purpose.
- 3 × 32 KiB I/O buffers (file transfer, MCP upload base64 decode, MCP body chunk), 2 × 16 KiB request bodies, 2 × 8 KiB JSON
  token arrays (REST and MCP; upstream had 12 × 16 KiB), 11 KiB HTTP request. Upstream's 1 MiB install chunk and 144 KiB NACP
  buffer left with the features.

## Touch API (new)

Coordinates are **screen pixels, origin top-left: x 0–1279, y 0–719**. Values must be plain JSON integers. Out-of-range values,
strings, fractions, booleans or missing fields get **HTTP 400** with a message (MCP: a tool result with `isError: true`).
`durationMs` must be ≥ 1, and anything above 10000 is capped at 10000 (like the button endpoints).

```
POST /input/touch  {"x": 640, "y": 360}                         tap (finger down 100 ms)
POST /input/touch  {"x": 640, "y": 360, "durationMs": 1500}     press and hold
POST /input/swipe  {"x1": 640, "y1": 600, "x2": 640, "y2": 150, "durationMs": 300}
POST /input/swipe  {... , "steps": 30}                           optional: number of moves (1-1000, default durationMs/16)
```

MCP tools `touch` and `swipe` take the same arguments, plus the usual optional `screenshot` / `screenshotDelayMs`.

How it works: libnx `hiddbgSetTouchScreenAutoPilotState` (hid:dbg command 11) makes hid report our finger instead of the real
panel, and `hiddbgUnsetTouchScreenAutoPilotState` (command 12) gives the panel back. **While our finger is set, real touches are
ignored**, so every touch/swipe ends by lifting the finger (an empty touch list), waiting 50 ms, and unsetting. That happens even when
a step fails. `/input/clear` and going to sleep also unset. Written from the libnx headers and source only (sys-botbase uses the same
two calls but is GPL, so we didn't look at its code).

The touch screen only works in **handheld mode**. Docked behaviour: see "Console results" below.

## Build

```bash
wsl.exe -d Ubuntu --exec bash -lc '/mnt/c/Users/JohnDoe/Projects/suwayomi-nx/tools/sys-autopilot-nx/build.sh'
```
Options: `--log` (writes `log = true` into the SD config), `--app` (also builds the dev `.nro`), `--no-test`.
It copies the source to `~/build/suwayomi-nx/sys-autopilot-nx/src`, runs the host tests, builds, prints the memory numbers, runs the
safety gate, and writes the SD card layout to `~/build/suwayomi-nx/sys-autopilot-nx/sd/`
(Windows: `\\wsl.localhost\Ubuntu\home\johndoe\build\suwayomi-nx\sys-autopilot-nx\sd`):

```
atmosphere/contents/4200000000004150/exefs.nsp
atmosphere/contents/4200000000004150/flags/boot2.flag
config/sys-autopilot/config.ini          token from tests/e2e/device/.env, hostname = switch-dev
switch/sys-autopilot-nx-dev.nro          only with --app
```

Note: the `.nsp` hash changes even when only comments change. The linker's build ID also covers the debug info (line numbers, build
folder path). To check that two builds run the same code, compare the program sections instead
(`aarch64-none-elf-objcopy -O binary --only-section=.text` etc.).

The bearer token lives only in `tests/e2e/device/.env` (git-ignored) and in the SD card's `config.ini`. Never put it in docs, chat or
a command line. `scripts/sa.sh` reads it from `.env` for you:

```bash
tools/sys-autopilot-nx/scripts/sa.sh GET /status
tools/sys-autopilot-nx/scripts/sa.sh POST /input/touch '{"x":640,"y":360}'
tools/sys-autopilot-nx/scripts/sa.sh -o shot.jpg GET /screenshot
```

Host tests alone (no devkitPro needed): `tests/run.sh`. `test_touch.c` holds the touch/swipe argument table (add rows there),
`test_mcp.c` tests the MCP layer end to end with the hardware stubbed (`stubs.c`).

Install, update and remove on the console: `docs/SETUP.md` B.2–B.4. Removing it completely = delete
`atmosphere/contents/4200000000004150/` on the SD card (and optionally `config/sys-autopilot/`).

## Dev flavor (.nro)

`build.sh --app` also builds upstream's dev flavor (`app/`), which runs the same server as a normal homebrew app (start it from
hbmenu, press + to quit). Handy for touch experiments without a reboot. The PC can reach it, but it can't reach the PC (NAT), so read
its on-screen output.

## Console results

WP-00b, 2026-10-25, on the owner's console (v1 Erista, FW 22.5.0, AMS 1.11.2, emuMMC; other sysmodules: sys-clk, emuiibo,
MissionControl, sys-patch, nx-ovlloader). Three boots, all clean: no new file in `atmosphere/crash_reports/` or `atmosphere/fatal_errors/`.

| Check | Result |
|---|---|
| Boot with sys-autopilot-nx | clean, three times (boot 1 with the first build, boots 2 and 3 with the final one) |
| `/status` | by IP (`<switch-ip>`): yes, first try every boot. `switch-dev.local`: resolves on Windows, **not inside WSL** (use the IP) |
| Auth / removed routes | no or wrong token → 401; `/install`, `/settings/theme` → 404 |
| `/files` | list, upload 4 KB, `/files/hash` = PC's SHA-256, download identical, delete: all fine |
| Button tap | RIGHT moves the HOME menu selection (virtual Pro Controller appears as an extra controller) |
| **First `/screenshot`** | **no crash** (upstream #28 crashed `am` here). JPEG 1280×720, ~190–215 KB, 0.1–0.7 s |
| **Touch (handheld)** | tap on a tile selects it; swipe on the game row scrolls it (with momentum). Real finger touches work again afterwards. Bad input → 400 |
| Sleep/wake | handled (sockets + controller released before sleep), answers right after wake. Transient mDNS errors while Wi-Fi reconnects, then it rebuilds its sockets by itself |
| 30 min idle | `/status` and screenshot still fine |
| Wi-Fi off/on (airplane mode) | sockets rebuilt automatically when the IP comes back |
| Heap | 304,768 bytes in use idle; peak 458,672 of 655,360 after MCP `read_file` tests (see "Memory" for the one case that answers "out of memory") |
| Docked touch | **not tested yet** (owner skipped it; possible small follow-up) |

Gotcha seen on the way: if the console is in airplane mode, nothing answers. Turning Wi-Fi back on is enough; the sysmodule recovers by itself.

## Files

Upstream files we changed: `package.json`, `sys-autopilot.json` (services), `default-config.ini`, `source/main.c`,
`source/common/{routes,mcp,mcp_tools.h,server,http,power,input,apiargs,screen,netif,device_info,config,mdns,json.h}`,
`scripts/gen_tools.py`, `app/source/main.c`, `tests/{run.sh,test_mcp.c,stubs.c}`. New: `source/common/battery.{c,h}`,
`tests/test_touch.c`, `build.sh`, `scripts/sa.sh`, `README.md`, `LICENSE`. To see every line we changed:
`git diff --no-index` against a checkout of upstream at the pinned commit (WSL: `~/build/suwayomi-nx/sys-autopilot-upstream`).
