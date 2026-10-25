# WP-01 Repo scaffold, CMake, doctest, CI

Model: **Opus** · Size: M · Needs: WP-00 · Owner: decide GitHub repo (yes/no, public/private) and confirm licence (D8)

## Goal
A real repo with the target layout from CLAUDE.md, building three things from one CMake project:
desktop app, Switch .nro, and a host unit-test binary. CI runs if a GitHub repo exists.

## Read first
`CLAUDE.md` (layout), `docs/DECISIONS.md` D1–D3, D7, D8, D10, D11. borealis_template's CMakeLists.txt. Skim Switchfin's
top-level CMakeLists.txt and `.github/workflows` for patterns.

## Deliverables
- [x] `git init`, `.gitignore` (build dirs, `tests/e2e/out/`, `tests/e2e/device/.env`, `node_modules`), `LICENSE`, `CREDITS.md`, `README.md` stub
- [x] borealis added as a git submodule at `library/borealis`, **pinned to a commit** (record it in DECISIONS.md)
- [x] CMake options: `PLATFORM_DESKTOP`, `PLATFORM_SWITCH`, `USE_DEKO3D`, `SNX_TEST_HOOKS`, `SNX_BUILD_TESTS`
- [x] Static libs per module (`snx_core`, `snx_net`, `snx_api`, `snx_image`, `snx_platform`) + app target. Empty placeholders with one function each
- [x] Each module folder has `README.md` (purpose, headers, threading, tests)
- [x] doctest vendored in `third_party/`. `tests/unit/` with one passing test per module, wired into `ctest`
- [x] nlohmann/json vendored (single header)
- [x] App shows a "Hello Suwayomi" borealis screen on desktop and Switch. Version string from CMake
- [x] `tools/build.sh desktop|switch|tests` wrappers (build dirs under `~/build/suwayomi-nx/`)
- [x] Coverage target (`tools/build.sh coverage` → gcovr HTML in `tests/out/coverage/`)
- [x] CI workflow: host build+tests, Switch build in `devkitpro/devkita64` (artifact: .nro). Skipped if no repo, but the file is still added
- [x] Build with `-g` (kept in the `.elf`, stripped in the `.nro`) and archive the `.elf` next to every `.nro` build, so crash logs can be symbolized later
- [x] First run of a *report-only* `nm` scan over the Hello `.elf` for the SAFETY.md S2/S3 symbol lists. Save it to `docs/safety-baseline.txt`
      (WP-03b turns it into a failing gate)
- [x] CLAUDE.md "Commands" section filled in

Done 2026-10-25 (Opus). Pin `5f08b28` + layout: D25. Two json copies: D26. CI: D27. Banned library functions → stand-ins: D28 (owner OK).

## Acceptance
`tools/build.sh tests` green. Desktop app opens in WSLg. `.nro` deploys and shows Hello via the WP-00 spike scripts.
CI green (if repo).

## Sonnet-safe chunks
- Writing the module README stubs from a template Opus provides.
- `.gitignore`/`CREDITS.md`/`README.md` stubs.

## Gotchas
Borealis needs `resources/` next to the binary on desktop and in romfs on Switch. Remove `-fno-rtti/-fno-exceptions` if present.
