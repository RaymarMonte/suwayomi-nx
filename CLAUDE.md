# suwayomi-nx

A native Nintendo Switch homebrew client (.nro) for a Suwayomi-Server manga library.
C++17, UI built with xfangfang/borealis. The same code also builds as a desktop app (Linux/WSLg) so we can test it on the PC.
The project is written almost entirely by Claude, and the owner is not a C/C++ developer. So:
explain decisions in plain words, keep code simple and well-named, and never assume the owner can debug C++ by hand.

## Session protocol (read this first, every session)

1. Read `docs/STATUS.md`, then the one work-package file you were given (`docs/wp/WP-xx-*.md`).
   Do **not** read other WP files, the whole roadmap, or modules your WP does not touch.
2. Read only the *interfaces* (`*.hpp` + the module's `README.md`) of modules you depend on, not their `.cpp`.
3. Do exactly one WP. If it turns out too big, stop at a clean, green point, split the rest into `WP-xxb`
   in `docs/ROADMAP.md`, and say so in STATUS.
4. Before finishing: run the test commands for your layer (see below). Fix failures or write them down in STATUS.
5. Update `docs/STATUS.md` (it's a handoff note, ≤ 40 lines, overwrite the "Last session" part, don't append history),
   tick the WP checklist, add any new decision to `docs/DECISIONS.md`, and commit with message `WP-xx: <summary>`.
6. Don't change a decision in DECISIONS.md without the owner's OK. If one blocks you, say so and propose an alternative.

Model guidance: **Opus 5.5 is the default.** The owner values accuracy over token savings. Each WP names its model.
Sonnet is only for WPs/chores marked **S** in `docs/ROADMAP.md`. If you are Sonnet and hit an "Escalate to Opus if" trigger,
stop, write what you found in STATUS under "Escalation", and end the session. Don't push through.
An Opus session may delegate a WP's listed "Sonnet-safe chunks" to a Sonnet subagent with a tight spec, then review its output.

## Layout (target, created in WP-01)

```
src/core/      pure C++ (no curl/borealis/libnx): models, JSON mapping, GraphQL docs, reader math, cache policy
src/net/       HttpClient interface + curl implementation, worker pool, cancellation
src/api/       SuwayomiClient: typed async calls on top of net + core, auth (none/basic/JWT)
src/image/     decode (jpeg/png/webp), downscale, tiling, memory + SD disk caches
src/platform/  switch/ and desktop/ implementations of paths, network status, sleep/focus, memory stats
src/ui/        borealis activities/views (one folder per screen)
src/app/       main(), wiring, logging, test hooks
resources/     borealis resources, xml layouts, i18n, icons
tests/unit/    doctest, host only, fast
tests/integration/  host binary against a live Suwayomi test server
tests/e2e/     Node+TypeScript harness: drivers (desktop, switch via sys-autopilot), scenarios, goldens
tests/fixtures/     generated manga, recorded GraphQL responses, schema.graphql
tools/         scripts: build, deploy, test-server, fixtures, schema dump
tools/sys-autopilot-nx/  our slim sys-autopilot fork with touch (C, MIT, own Makefile, D19). It's a sysmodule: crash = system fatal
library/borealis/   git submodule, pinned commit (D1)
third_party/   vendored single headers: doctest, nlohmann/json
docs/          plan, architecture, testing, research, decisions, status, work packages
```

## Rules for code

- `core/` must never include curl, borealis, libnx or platform headers. That keeps it unit-testable on the PC.
- libnx/Switch-only code lives in `platform/switch/` behind an interface. Desktop gets a stub or real equivalent.
- UI code is only touched on the main thread. Background results come back through `brls::sync`.
- Every network or decode task takes a cancellation token, so leaving a screen cancels its work.
- Never hard-code memory sizes, cache budgets, prefetch counts or concurrency. Read them from `RuntimeProfile`. For now the app runs only in
  application mode (hold R on a game, profile `Full`) and refuses applet mode (Album) with a short message (D21, amends D12). Keep every limit
  in the profile anyway, so applet support (WP-21, nice to have) can come back later. Device tests run in application mode.
- No GPL code gets copied in (wiliwili, komga-switch are GPL). Apache/MIT code (borealis, switchfin,
  mangadex-reader) may be adapted, with a credit in `CREDITS.md`.
- Log with the app logger, not printf. In test builds the app also writes events (see `docs/TESTING.md`).
- **Safety first** (`docs/SAFETY.md`, D13–D15): never brick, never crash the console. All file writes go through `SafeFs`
  (our folder only, atomic). Never call system-writing APIs or `fatalThrow` (the `check-elf.sh` gate enforces this).
  Untrusted input gets size/dimension limits. Don't weaken the crash handler, the gates or the sys-autopilot allowlist without the owner's OK.

## Commands (filled in by WP-01..03, keep this section current)

Build and test commands run inside WSL Ubuntu: `wsl.exe -d Ubuntu --exec bash -lc '<cmd>'`
(`--exec`, not `--`: with `--` WSL runs the line through a shell first and `$VARS` get expanded too early. `-l` loads devkitPro's env and nvm).
Repo path in WSL: `/mnt/c/Users/JohnDoe/Projects/suwayomi-nx`. Build dirs go in `~/build/suwayomi-nx/...` (it's faster).
The repo is public (D23), so docs write `JohnDoe` for the real Windows user name and `<switch-ip>` for the Switch's LAN IP
(it's in `tests/e2e/device/.env`). Use the session's real working directory. Commits: repo-local noreply identity (D23);
push from WSL, where `gh` is logged in (SETUP.md C). `<repo>` below = the repo path in WSL.

- Unit tests: `<repo>/tools/build.sh tests` (ctest, one entry per module). Test-hooks variant: add `--test-hooks`.
- Coverage: `<repo>/tools/build.sh coverage` → `tests/out/coverage/index.html` (git-ignored)
- Desktop build/run: `<repo>/tools/build.sh desktop` → `~/build/suwayomi-nx/desktop/suwayomi-nx` (run from that folder).
  `tools/build.sh run [-- -d --log-file]` builds and opens it in WSLg. Headless check: `<repo>/tools/desktop-smoke.sh` (Xvfb).
  Any desktop run on Xvfb must hide WSLg's Wayland socket (empty `XDG_RUNTIME_DIR`), or the window opens on the owner's screen.
- Switch build: `<repo>/tools/build.sh switch` → `~/build/suwayomi-nx/switch/suwayomi-nx.nro`. Also archives .elf + .nro under
  `~/build/suwayomi-nx/archive/<build id>/` and writes `safety-report.txt` (S2/S3 scan, report-only until WP-03b).
- Device check (console on, Spelunky 2 first on HOME): `<repo>/tools/spike/wp01-hello.sh` (build → app mode → nxlink → screenshot → PLUS → log)
- App flags: `-d` debug log, `--log-file` → `<data>/logs/app.log` (Switch: `sdmc:/switch/suwayomi-nx/logs/app.log`)
- Test server start/stop: _TBD in WP-02_
- E2E desktop / device: _TBD in WP-03_

## Docs index

`docs/PLAN.md` overview · `docs/ROADMAP.md` WP list and models · `docs/ARCHITECTURE.md` · `docs/TESTING.md` ·
`docs/SUWAYOMI-API.md` · `docs/SAFETY.md` crash/brick safety · `docs/SETUP.md` install checklist · `docs/RESEARCH.md` references · `docs/DECISIONS.md` · `docs/STATUS.md`
