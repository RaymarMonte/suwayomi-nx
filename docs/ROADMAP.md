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
| [WP-12](wp/WP-12-mvp-hardening.md) | MVP hardening, soak test, **v0.1 release** | 11 | Opus | M | Try it for real | done (v0.1.0, 2026-10-30) |
| [WP-22](wp/WP-22-portrait-reader.md) | **Portrait reader**: hold the Switch sideways, pages fill the tall screen (owner's top priority, 2026-10-30). **Runs next, after WP-12** | 12 | Opus | M | **Yes**: try it in your hands; console | Done (owner tried it 2026-10-30) |
| WP-13 | **Zoom & pan** in the paged reader, both orientations: pinch, double-tap and ZL/ZR zoom, drag and stick pan, sharper pages when zoomed, double-page spreads split | 22 | Opus | M | **Yes**: try it in your hands; console | todo |
| WP-13b | **Webtoon reader**: long strips as one continuous strip, smooth stick and touch scrolling with flicks, both orientations, zoom in the strip | 13 | Opus | L | **Yes**: try it in your hands; console | todo |
| WP-18 | **Offline 1: download chapters to the SD card**: a queue on the Switch, a storage limit, delete; the reader reads downloaded pages first | 13b | Opus | L | – | todo |
| WP-18b | **Offline 2: read away from home**: start without the server, show the downloaded manga, keep offline progress and send it when the server is back | 18 | Opus | M | **Yes**: try it away from home | todo |
| WP-15 | **Updates + history**: new chapters across the library, "update library" with progress, reading history with continue | 12 | Opus (History screen: Sonnet-safe chunk) | M | – | todo |
| WP-24 | **Library tools**: search my library, manage categories (create, rename, order, delete), move manga between categories | 12 | Opus | M | – | todo |
| WP-23 | **Test source** for the test server: an offline extension store and a source whose chapters the server can download, so extensions, server downloads and source filters can be tested | 02 ∥ any (not with 15's seed change) | Opus | M | Maybe: install a build tool | todo |
| WP-14 | **Browse**: sources by language, popular / latest / search, search across sources, filters, preview, add to library with a category | 24, 23 | Opus | M | – | todo |
| WP-16 | **Extensions**: installed / updates / available, install, update, uninstall, extension repos, permission errors | 14, 23 | Opus | M | – | todo |
| WP-25 | **Tracking**: a manga's tracker entries (AniList, MyAnimeList, …): find and link, status, score, chapter | 12 | Opus | M | **Yes**: a tracker account to try it | todo |
| WP-19 | Settings polish, i18n, theming, optional HTTPS, config saves off the main thread, small reader leftovers | 12 | Opus (string extraction: **H**) | M/L | – | todo |
| WP-20 | User docs (README for users, screenshots), release notes, (optional) hardware CI runner | 12 ∥ any (docs only) | **S** | S | Publish releases | todo |
| WP-17 | **Download on server** (nice to have): a chapter action and the server's download queue, like Sorayomi | 15, 23 | **S** (Opus if 15's polling doesn't fit) | S | – | todo |
| WP-26 | **In-app updates** (nice to have): fetch the new .nro from GitHub Releases, checksum-checked, old one kept | 19 | Opus | M | **Yes**: OK for a safety-rule exception (D15, S11) | todo |
| WP-21 | Applet mode (Album) support, `Lite` profile, and a crash ending that doesn't take the system down from the Album (D41). **Nice to have** (D21) | 12 | Opus | M | **Yes**: decide if wanted; console | todo |

**Touch first (D16):** every UI WP from WP-08 on ships touch for its own screens, tested on desktop with the mouse
(plus a test hook for multi-finger gestures) and on the console with the touch-capable sys-autopilot (D19) or a manual checklist.
**Stock clocks (D17):** performance is judged at stock clocks only.
**Application mode only (D21):** every WP builds, tunes and tests for application mode (hold R on a game). Applet mode only gets the
"can't run from the Album yet" screen (WP-08) until WP-21.
**Order after WP-22 (owner, 2026-10-30, D47):** WP-13 (zoom) → WP-13b (webtoon) → WP-18 → WP-18b (offline) → WP-15 (updates,
history) → WP-24 (library tools) → WP-14 (browse) → WP-16 (extensions) → WP-25 (tracking) → WP-19 (polish). WP-23 (test source)
and WP-20 (user docs) touch no app code and can run alongside; WP-23 must be done before WP-14. Nice to have, when the owner asks:
WP-17, WP-26, WP-21, WP-08b. Phase 3 is listed in that order, not by number.

### Sonnet / Haiku work outside the WPs

These come up as standalone short sessions. Sonnet is fine because each has a clear, checkable result:
- Adding fixture manga or test cases to an existing table-driven test ("add 5 cases like these").
- Updating golden screenshots after an *intended* UI change (the harness does the diff, Sonnet just re-runs and commits).
- Docs: README sections, keeping CLAUDE.md commands current, changelog entries (**H**).
- A bug with a failing regression test already written by Opus ("make this test pass without changing the test").
- CI tweaks such as version bumps or caching. Not new pipelines.

Never Sonnet: anything touching threading, memory budgets, the image pipeline, auth/token logic, the reader,
on-device debugging, **the safety layer (`src/app/crash/`, `SafeFs`, `check-elf.sh`, allowlists)**, or a new pattern other code will copy.

## How the Phase 3 WPs get their full docs (D47)

The stubs below are kept short on purpose. Right before a WP runs, a fresh Opus **planning session** writes its
`docs/wp/WP-xx-*.md` from `wp/_TEMPLATE.md`: it reads STATUS.md, the stub, and the READMEs and headers of the modules the WP
touches, checks the stub's queries on the test server, asks the owner the product questions, updates the stub and the table if the
plan changes them, and commits `WP-xx: plan ...`. No code. Effort: **max** for WP-13, 13b, 18, 18b, 21 and 26 (GPU, memory, sync,
safety), **xhigh** for the rest. A planning session only changes docs, so it may run while the previous WP runs, on its own branch
(a worktree, merged after that WP), when it doesn't depend on that WP's result: WP-15 can be planned during WP-18b, WP-18b can't
be planned during WP-18. Then the WP itself runs in its own session, as before.

## Phase 3 stubs (roadmap pass 2026-10-30, D47)

- **WP-13 Zoom & pan (paged reader).** Pinch and double-tap zoom, ZL/ZR zoom, drag to pan and stick pan while zoomed (today the
  stick turns pages, D44), in landscape and portrait (WP-22's transform and touch mapping, D46). Zoomed pages get sharper: decode
  again at a bigger box, or only the part on screen, within `maxDecodeScale` and the decode budget (D39); zoom limits come from the
  runtime profile. Double-page spreads (wider than 1.2 × tall, D44) can be split into halves, order mirrored for right to left,
  with a setting; in portrait too (D46 left this). Also: holding the D-pad in portrait turns several pages (D46). Owner tries it.
- **WP-13b Webtoon reader.** Direction `vertical` (already in config.json, D36) becomes one continuous strip: pages and long strips
  cut into tiles (`core::tileWindow`, `webtoonTilesAround`, D39), only the tiles near the screen decoded and on the GPU (S7), smooth
  stick and held-D-pad scrolling, touch drag with flick momentum, page and chapter boundaries, progress by the page at the
  screen's centre, the next chapter joined on. Both orientations (a strip in portrait is likely the best way to read one on the
  Switch). Zoom in the strip reuses WP-13's math. Fixture: Gamma Webtoon (800 × 12000 WebP strips, TESTING.md). Owner tries it.
- **WP-18 Offline 1: download to the SD card.** "Download" means to the Switch, like Mihon (owner, D47). The chapter row menu and
  the bulk actions get Download / Delete download; a queue on the Switch downloads one chapter at a time in the background through
  the image path (the same page URLs, so the test server's Local source works), stops at sleep, and goes on after a restart. Every
  write goes through SafeFs, atomic, with the free-space guard and a storage limit from the profile (S5, D14). A downloaded chapter
  reads from the SD card first, so pages turn without the network. Where downloads show: the planner's default is a new
  **Downloads** tab (queue, downloaded manga, space used, delete), to confirm while planning.
- **WP-18b Offline 2: read away from home.** Starting without the server (no network, or it can't be reached) opens the downloaded
  manga instead of an error. Progress made offline is kept on the SD card (a small queue, written atomically) and sent once the
  server answers, with conflict rules (the server moved further meanwhile, a chapter read on another device). The same queue fixes
  D45's leftover: a save that fails while the reader closes is never tried again. The owner tries it away from home.
- **WP-15 Updates + history.** Updates tab: new chapters across the library, newest first (`chapters(filter: {inLibrary: {equalTo:
  true}}, order: [{by: FETCHED_AT, byType: DESC}])`, works on the test server), grouped by day; open the chapter or its manga.
  "Update library" (`updateLibrary`, progress by polling `libraryUpdateStatus`) moved here from WP-17: the first screen that polls
  a server job, a pattern WP-17 copies, so Opus. History tab: chapters by `LAST_READ_AT` (filter `lastReadAt > 0`, works), one
  row per manga (a bulk update stamps many chapters at once, D43: dedupe across pages, like D42), a tap continues. Test server:
  the seed needs a chapter that appears after the first scan, for a real "new chapter". Sonnet-safe chunk: the History screen.
- **WP-24 Library tools.** Search my library by title (the server's `mangas` filter or the loaded list: to decide), manage
  categories (`createCategory`, `updateCategory`, `updateCategoryOrder`, `deleteCategory`), move manga between categories from the
  library and the details screen (`updateMangaCategories`), through a category picker that WP-14 reuses. The Library tab keeps
  its model (D42).
- **WP-23 Test source (test infrastructure).** The test server has only the Local source: browse works offline (popular, latest,
  search; only a Sort filter), there are no extensions and no extension repo, and the server can't download Local chapters
  (`enqueueChapterDownloads` empties the queue, `isDownloaded` stays false; tried 2026-10-30). WP-14's filters, WP-16 and WP-17
  need more, still without the internet once set up (TESTING.md). Ideas to check: a small extension store served from the PC
  (index + pinned extension packages with SHA-256, downloaded once like the jar, D29); for a source with downloadable chapters, an
  extension that reads from an address we set (the old Suwayomi/"Tachidesk" extension is no longer in keiyoushi's repo, checked
  2026-10-30) or our own tiny extension built once (Android build tools in WSL: an owner install). Check licences before pinning.
  It touches only `tools/test-server/`, so it can run alongside, except during WP-15's seed change.
- **WP-14 Browse.** Sources by language (Local first), popular / latest / search per source, a search across sources (Suwayomi-JUI
  has one), filters (the server's `Filter` types: sort, text, select, check and tri-state first; groups if they fit), a preview of
  a manga not in the library (the details screen with "Add to library"), adding with WP-24's category picker. Copies the Library
  pattern (D42). Test seed: some Local manga left out of the library.
- **WP-16 Extensions.** Inside Browse (like Mihon: Sources | Extensions): installed, with updates, available, by language; install,
  update, uninstall (`updateExtension`), check for updates (`fetchExtensions`), extension repos (`extensionStores`,
  `addExtensionStore`, `removeExtensionStore`: a fresh stable server has none), content warnings, multi-user permission errors
  (`INSTALL_EXTENSIONS`, D38's Permission). Tested against WP-23's store.
- **WP-25 Tracking.** On the details screen: the manga's tracker entries, find and link (`searchTracker`, `bindTrack`), change
  status / score / chapter (`updateTrack`), unlink (`unbindTrack`). Logging in to a tracker needs a browser (OAuth), so trackers are
  logged in in the server's web UI and the Switch shows which are (`trackers { isLoggedIn }`); a username/password tracker
  (`loginTrackerCredentials`) may log in from the Switch. Tests can't use real tracker accounts: recorded answers and contract
  tests, plus the owner's own try.
- **WP-19 Polish.** All settings screens, translations (Haiku extracts strings), light/dark, optional HTTPS via curl+mbedTLS.
  Real target: the owner's server already speaks HTTPS through Tailscale Serve (D18 "Update": Let's Encrypt ECDSA certificate, chain
  YE2 → Root YE → ISRG Root X2, TLS 1.2 works). The Switch would use it through a Tailscale travel router. Check that the Switch trusts that chain.
  Leftovers: `config.json` saves off the main thread (a save holds the loop for a moment and a short tap can be lost, D44, D46), a
  download percentage under the reader's spinner (D44), per-manga reader settings kept per server (D44), auto-rotate by the motion
  sensor with a lock (D46, optional). Split if it's too big.
- **WP-20 User docs.** A README for users (install, hold R, first run, controls, screenshots from the desktop build), release
  notes. Releases already exist (`tools/release.sh`, D45). The hardware CI runner (device E2E on a self-hosted runner) stays
  optional; this pass suggests skipping it (the console must be on and someone must watch for fatals). Docs only.
- **WP-17 Download on server (nice to have).** A chapter action "Download on server" (`enqueueChapterDownloads`), the server's
  queue with progress, remove, start / stop (`downloadStatus` polled like WP-15; the `downloadChanged` subscription needs a
  WebSocket we don't have), delete server downloads (`deleteDownloadedChapters`). Needs WP-23's source. Sonnet once WP-15's
  polling and WP-23 exist; Opus if they don't fit.
- **WP-26 In-app updates (nice to have).** Settings → About checks GitHub Releases (needs WP-19's HTTPS), shows what's new, and on
  the user's OK downloads the .nro, checks it against `SHA256SUMS.txt`, and swaps it in atomically, keeping the old one. Today
  D15/S11 rule out self-update and SafeFs refuses to write any `.nro` (D33): the owner's OK for that exception comes first.
- **WP-21 Applet mode (nice to have, D21).** Only if the owner still wants it after the MVP. Make the app usable when opened from the Album:
  measure applet memory (alone and with a game suspended), build the `Lite` profile (ARCHITECTURE.md "Lite" column, D12), replace the
  refusal screen with a one-time tip on how to open it with R, check the software keyboard, HOME/return and networking with 2 socket sessions,
  crash drills in applet mode, offline downloads (WP-18) one at a time with small buffers, and run every scenario in both modes (the device
  test matrix doubles). Background: RESEARCH.md "Applet mode vs application mode". Always Opus (memory, crash safety).

## How to start a session (copy-paste prompt)

> Plan WP-xx. Follow "How the Phase 3 WPs get their full docs" in docs/ROADMAP.md.

> Work on WP-xx. Follow the session protocol in CLAUDE.md.

Pick the model in the app's model selector to match the table above before sending (planning: always Opus, effort as above).
