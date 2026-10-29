# Roadmap: work packages

One WP = one session. Status: `todo` · `doing` · `done` · `blocked`.
"Needs" = WPs that must be `done` first. WPs with no dependency on each other can run in parallel sessions,
but only if they don't touch the same files (marked ∥).

Model: **Opus 5.5 is the default** (the owner prioritises accuracy). **S** = Sonnet is allowed *only* where the path is
clear and mechanical: copying an established pattern, scripts, docs, packaging. **H** = Haiku for pure chores.
Opus WPs list "Sonnet-safe chunks" that Opus may hand to a Sonnet subagent with a tight spec, then review.
Size is a rough guide to session length: S = small, M = medium, L = large (consider splitting).

| WP | Title | Needs | Model | Size | Owner action? | Status |
|---|---|---|---|---|---|---|
| [WP-00a](wp/WP-00a-toolchain-and-console-prep.md) | Toolchain + console prep (backups, SD facts, sysmodule list). Installs nothing on the console | – | Opus | M | **Yes**: sudo, SD card, console settings | done |
| [WP-00b](wp/WP-00b-sys-autopilot-nx-fork.md) | sys-autopilot-nx: slim fork with touch, installed and verified (D19) | 00a | Opus | M | **Yes**: SD card, reboots, watch for crashes | done |
| [WP-00](wp/WP-00-toolchain-and-device-loop.md) | Device loop spike (borealis demo: deploy, launch, input, touch, screenshot, memory probe) | 00b | Opus | M | **Yes**: console, handheld | done |
| [WP-01](wp/WP-01-repo-scaffold-and-ci.md) | Repo scaffold, CMake, doctest, CI | 00 | Opus | M | GitHub repo (optional) | done |
| [WP-02](wp/WP-02-test-server-and-fixtures.md) | Suwayomi test server + fixture manga + schema dump | 01 | Opus | M | Firewall prompt | done |
| [WP-03](wp/WP-03-e2e-harness.md) | E2E harness: drivers + smoke scenario | 01, 02 | Opus | L | Console on + reachable | done |
| [WP-03b](wp/WP-03b-safety-layer-and-crash-drills.md) | **Safety layer**: crash containment, write jail, banned-API gate, crash drills (PC side; device runs moved to WP-03c) | 03 | Opus | M | – | done |
| [WP-03c](wp/WP-03c-crash-drills-on-the-console.md) | Crash drills on the console (WP-03b's device half), optional control case | 03b | Opus | S/M | **Yes**: console; boost-mode OK (D34); control-case OK; D35 | done |
| [WP-04](wp/WP-04-core-models-graphql.md) | core: models, JSON, GraphQL docs, contract tests | 02 | Opus | M | – | done |
| [WP-05](wp/WP-05-net-http-client.md) | net: HttpClient, worker pool, cancellation | 01 ∥04 | Opus | M | – | done |
| [WP-06](wp/WP-06-api-suwayomi-client.md) | api: SuwayomiClient + auth modes | 04, 05 | Opus | M | – | done |
| [WP-07](wp/WP-07-image-pipeline-and-cache.md) | image: decode, downscale, tile, caches, memory budget | 05 | Opus | L | – | done |
| [WP-08](wp/WP-08-app-shell-and-setup.md) | App shell, settings, logging, test hooks, server setup/login | 03b, 06 (its applet-guard device drill: 03c) | Opus | L | **Yes**: watched the applet-guard run on the console (D41) | done |
| WP-08b | All controllers merged on the Switch: borealis's `getControllersConnectedCount` stops at the first empty slot, so a pad in slot 2 (sys-autopilot's after a real controller left slot 1) is ignored. Replace borealis's `switch_input.cpp` in our build with a fixed copy (Apache-2.0, CREDITS.md), D40. **Nice to have for now** (owner, 2026-10-29) | 08 | Opus | S | **Yes**: a second controller to check it | todo |
| [WP-09](wp/WP-09-library-screen.md) | Library screen (categories + cover grid) | 07, 08 | Opus | M | **Yes**: watched the console run (`library-browse`, new smoke golden) | done |
| [WP-10](wp/WP-10-manga-details.md) | Manga details + chapter list | 09 | Opus | M | – | done |
| [WP-11](wp/WP-11-paged-reader.md) | Paged reader + progress sync | 10 | Opus | L | – | done |
| [WP-12](wp/WP-12-mvp-hardening.md) | MVP hardening, soak test, **v0.1 release** | 11 | Opus | M | Try it for real | in progress (PC side done) |
| [WP-22](wp/WP-22-portrait-reader.md) | **Portrait reader**: hold the Switch sideways, pages fill the tall screen (owner's top priority, 2026-10-30). **Runs next, after WP-12** | 12 | Opus | M | **Yes**: try it in your hands; console | todo |
| WP-13 | Webtoon / continuous reader + zoom & pan, in both orientations | 12, 22 | Opus | L | – | todo |
| WP-14 | Browse sources, search, add to library | 12 ∥13 | Opus | M | – | todo |
| WP-15 | Updates feed + reading history | 12 ∥13 | **S** | M | – | todo |
| WP-16 | Extensions: list, install, update, uninstall | 14 | Opus | M | – | todo |
| WP-17 | Server downloads queue + library update trigger | 12 | **S** | S | – | todo |
| WP-18 | Offline reading (chapters to SD) | 13, 17 | Opus | L | – | todo |
| WP-19 | Settings polish, i18n, theming, optional HTTPS | 12 | Opus (string extraction: **H**) | M | – | todo |
| WP-20 | Release packaging, user docs, (optional) hardware CI runner | 12 | **S** | S | Publish releases | todo |
| WP-21 | Applet mode (Album) support, `Lite` profile, and a crash ending that doesn't take the system down from the Album (D41). **Nice to have** (D21) | 12 | Opus | M | **Yes**: decide if wanted; console | todo |

**Touch first (D16):** every UI WP from WP-08 on ships touch for its own screens, tested on desktop with the mouse
(plus a test hook for multi-finger gestures) and on the console with the touch-capable sys-autopilot (D19) or a manual checklist.
**Stock clocks (D17):** performance is judged at stock clocks only.
**Application mode only (D21):** every WP builds, tunes and tests for application mode (hold R on a game). Applet mode only gets the
"can't run from the Album yet" screen (WP-08) until WP-21.
**Order after WP-12:** WP-22 (portrait reader) comes first: reading with the console held sideways is one of the main reasons
for the app (owner, 2026-10-30). It got the next free number, so the table isn't in number order there; WP-13 builds on it.

### Sonnet / Haiku work outside the WPs

These come up as standalone short sessions. Sonnet is fine because each has a clear, checkable result:
- Adding fixture manga or test cases to an existing table-driven test ("add 5 cases like these").
- Updating golden screenshots after an *intended* UI change (the harness does the diff, Sonnet just re-runs and commits).
- Docs: README sections, keeping CLAUDE.md commands current, changelog entries (**H**).
- A bug with a failing regression test already written by Opus ("make this test pass without changing the test").
- CI tweaks such as version bumps or caching. Not new pipelines.

Never Sonnet: anything touching threading, memory budgets, the image pipeline, auth/token logic, the reader,
on-device debugging, **the safety layer (`src/app/crash/`, `SafeFs`, `check-elf.sh`, allowlists)**, or a new pattern other code will copy.

WP-13 … WP-21 are kept short on purpose. The session *before* each one (or an Opus planning session)
writes its full `docs/wp/WP-xx-*.md` using the same template, once the MVP has shown what we actually need.

## Phase 3 stubs

- **WP-13 Webtoon reader + zoom.** Works in landscape and in WP-22's portrait (the rotation math and touch mapping are
  WP-22's; a webtoon strip in portrait is probably the best way to read one on the Switch). Continuous vertical scroll built from image tiles, smooth stick scrolling and touch flick scrolling, pinch/double-tap zoom and drag-to-pan (D16) plus stick zoom/pan,
  double-page spread detection/splitting. Tile window and zoom resolution come from the runtime profile. Always Opus (GPU/memory/perf).
- **WP-14 Browse.** Sources list (by language), popular/latest/search per source, filters (basic), manga preview, add to library + category.
  Copies the Library screen pattern.
- **WP-15 Updates + history.** Recent chapter updates across library, "continue reading" history. Copies the list pattern.
- **WP-16 Extensions.** List installed/available/updatable, install/update/uninstall mutations, error states. Respects multi-user permissions.
- **WP-17 Server downloads.** Enqueue/dequeue chapter downloads on the server, show queue and progress (polling or subscription), trigger library update.
- **WP-18 Offline.** Download chapter pages to SD, offline library view, progress queue that syncs when back online.
  Opus because of the sync/conflict rules.
- **WP-19 Polish.** All settings screens, translations (Haiku extracts strings), light/dark, optional HTTPS via curl+mbedTLS.
  Real target: the owner's server already speaks HTTPS through Tailscale Serve (D18 "Update": Let's Encrypt ECDSA certificate, chain
  YE2 → Root YE → ISRG Root X2, TLS 1.2 works). The Switch would use it through a Tailscale travel router. Check that the Switch trusts that chain.
- **WP-20 Release.** GitHub Releases with .nro + checksums, user README with screenshots, changelog (Haiku), optional self-hosted runner that runs device E2E.
- **WP-21 Applet mode (nice to have, D21).** Only if the owner still wants it after the MVP. Make the app usable when opened from the Album:
  measure applet memory (alone and with a game suspended), build the `Lite` profile (ARCHITECTURE.md "Lite" column, D12), replace the
  refusal screen with a one-time tip on how to open it with R, check the software keyboard, HOME/return and networking with 2 socket sessions,
  crash drills in applet mode, offline downloads (WP-18) one at a time with small buffers, and run every scenario in both modes (the device
  test matrix doubles). Background: RESEARCH.md "Applet mode vs application mode". Always Opus (memory, crash safety).

## How to start a session (copy-paste prompt)

> Work on WP-xx. Follow the session protocol in CLAUDE.md.

Pick the model in the app's model selector to match the table above before sending.
