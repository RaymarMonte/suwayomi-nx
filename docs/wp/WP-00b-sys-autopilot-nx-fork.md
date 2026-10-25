# WP-00b sys-autopilot-nx: slim fork with touch

Model: **Opus** · Size: M (can run long because of reboots) · Needs: 00a · Owner: **present** (SD card, reboots, watching for crash screens, handheld touch check)

## Goal
Our own build of sys-autopilot (D19), installed and stable on the owner's console (v1 Erista, FW 22.5.0, AMS 1.11.2, emuMMC).
It uses much less sysmodule memory than stock v1.5.0, it physically can't do the dangerous things (install titles, change DNS/settings,
airplane mode, power), and it can inject **touch**, which our test harness needs because the app is touch first (D16).
A sysmodule crash is a system fatal, so this WP goes slowly and checks after every step.

## Read first
D19 in `docs/DECISIONS.md`, `docs/SAFETY.md` S10. Upstream (at the commit pinned in STATUS by WP-00a): `README.md` (REST + MCP sections),
`source/main.c`, `source/common/input.c`, `routes.c`, `mcp.c`, `apiargs.c`, `Makefile`, `sys-autopilot.json` (NPDM), `tests/run.sh`.
The **diff** of upstream PR #30 (not its discussion). libnx `hiddbg.h`: `hiddbgSetTouchScreenAutoPilotState`, `hiddbgUnsetTouchScreenAutoPilotState`, and `HidTouchState` in `hid.h`.
Don't read sys-botbase code beyond the idea that it uses the same two calls: it's GPL.

## Deliverables
- [x] `tools/sys-autopilot-nx/`: upstream copied in at the pinned commit (not a submodule). `README.md` on top says what it is, the upstream commit,
      what we changed and why. `LICENSE` with the MIT text naming the upstream author (upstream declares MIT in `package.json` but ships no LICENSE file).
      Entry in `CREDITS.md`. Version string `1.5.0-nx.1`
- [x] **Measure first**: build unmodified and record `.bss` / ELF size (`aarch64-none-elf-size`). Every later step records the new numbers
- [x] **Slim**: inner heap and big static buffers reduced (PR #30's numbers are a starting point, not gospel. Check each buffer's real use).
      Remove from the build: title installer (`install.c`, `nx_ext.c`), system settings (incl. `audctl`), DNS, power, titles, airplane, OAuth flow.
      Keep: `/status`, `/screenshot`, `/input/*`, `/controller/*`, `/files*` (incl. hash), static bearer token auth, mDNS, optional file log, the PSC sleep handling.
      Trim the NPDM service list to what's left (fewer services = less to go wrong). Removed routes return 404, and their MCP tools are gone from `tools/list`
- [x] **No new aborts**: optional services fail softly (log + feature off). Only the truly required ones (sm, fs, hid:dbg, bsd) may abort at init, as upstream does
- [x] **Touch**: `POST /input/touch {"x","y","durationMs"?}` (tap, or hold when durationMs is longer) and
      `POST /input/swipe {"x1","y1","x2","y2","durationMs","steps"?}`, plus MCP tools `touch` and `swipe`.
      Screen coordinates 0–1279 × 0–719. Reject out-of-range values with HTTP 400. Durations capped at 10 s like upstream.
      Always release the finger and call `hiddbgUnsetTouchScreenAutoPilotState()` afterwards, and also on `/input/clear`, so real touches work again
- [x] Host tests (upstream's `tests/run.sh` style) for touch/swipe argument parsing: valid, missing fields, out of range, wrong types, huge numbers
- [x] Build in WSL → SD layout (`atmosphere/contents/4200000000004150/…`, `config/sys-autopilot/config.ini`). Write `tools/sys-autopilot-nx/build.sh`
- [x] Token: Claude generates a long random token. It goes in `config.ini` and `tests/e2e/device/.env`, plus a `.gitignore` entry for that file. Never in docs or chat
- [x] **Install + verify on the console, in this order, owner watching.** After each step, list `atmosphere/crash_reports/` and `atmosphere/fatal_errors/`:
      1. Copy files to SD (`log = true` for the first boots), reboot → clean boot to HOME
      2. `GET /status` from WSL (IP vs `.local`, record which works)
      3. `/files`: list our folder, upload a small file, `/files/hash` matches, delete it
      4. Button tap on the HOME menu (moves the cursor)
      5. **First `/screenshot`**, with no game open and nothing else writing to the SD. If it crashes: owner powers off, Claude removes the folder
         (SETUP.md B.2 undo), records the error code and crash report, and **stops the WP** (Escalation in STATUS)
      6. Touch, handheld mode: tap a HOME-menu icon (it gets selected/opens), swipe the game row (it scrolls). Screenshot each
      7. Sleep and wake once (owner presses POWER twice) → `/status` still answers
      8. Leave it idle 30 min → `/status` and a screenshot still work
- [x] Repeat steps 2, 5 and 6 after a second reboot
- [x] Docs: SETUP.md B.2–B.4 with the exact install steps for our build. TESTING.md "SwitchDriver" if the API differs from the plan.
      STATUS "Console facts": sys-autopilot-nx version, host/IP, memory sizes before/after, anything odd

## Acceptance
Two clean boots in a row, with `/status`, `/screenshot`, a button tap, a touch tap and a swipe all working, and no new files in
`atmosphere/crash_reports/` or `atmosphere/fatal_errors/`. `.bss`/ELF size is clearly below stock (record both). Host tests pass.

## Sonnet-safe chunks
- More host test cases for touch/swipe parsing, following the first few Opus wrote ("add 10 more like these").

## Gotchas
- A sysmodule only reloads on reboot. For quick touch experiments, upstream's dev `.nro` flavor (`make -C app`) runs as a normal app instead.
  But Switch → WSL connections don't work (NAT), so read its on-screen output, not `nxlink -s`.
- The touchscreen only works in handheld mode. Docked, injected touches may or may not reach apps. Test handheld, and note what docked does.
- While a touch autopilot state is set, real touches are ignored. That's why "always unset" is a deliverable.
- Keep title ID `4200000000004150`. To remove: delete that folder (SETUP.md B.2). Don't add a second sysmodule.
- Write only `config/sys-autopilot/` and `atmosphere/contents/4200000000004150/` on the SD in this WP (the owner OK'd both via D19).
