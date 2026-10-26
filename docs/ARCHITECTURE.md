# Architecture

## Module map and dependency rules

```
            ┌──────────── app (main, wiring, logger, test hooks) ────────────┐
            │                                                                 │
           ui (borealis screens)                                     platform/{switch,desktop}
            │        │                                                        ▲
            ▼        ▼                                                        │
           api     image ───────────────► net (HttpClient) ───────────────────┘
            │        │                      │
            └────────┴──────► core ◄────────┘        (arrows = "may include")
```

- `core` depends on nothing but the C++ standard library and nlohmann/json. **100% host-testable.**
- `net`, `api`, `image` depend on `core` (+ curl / decoders). Host-testable with fakes.
- `ui` depends on everything above plus borealis. It's tested through E2E only, so keep logic *out* of views.
  Views call small "view-model" classes in `ui/<screen>/…Model.hpp` that hold state and logic,
  and those models get unit tests too.
- `platform` hides libnx. Interfaces live in `platform/Platform.hpp`, with implementations in `platform/switch/` and `platform/desktop/`.

Every module has a `README.md` (≤ 30 lines: purpose, public headers, threading rules, how to test).
That README plus the public headers *is* the contract other sessions read.

## core
- **Models**: `Manga`, `Chapter`, `Category`, `Source`, `Extension`, `ServerInfo`, `PageList`, `ReadProgress`. Plain structs, `fromJson()`.
- **GraphQL**: `.graphql` documents in `src/core/graphql/*.graphql`, embedded as strings at build time. A `GqlRequest{doc, variables}` builder and a `GqlResponse` parser
  (data vs `errors[]`, partial data).
- **Reader math**: fit modes (width/height/screen/original), page order (LTR/RTL/vertical), spread detection (aspect > 1.2),
  tile slicing plan for tall images, prefetch window selection.
- **Policies**: LRU with byte budget (generic), retry/backoff policy, progress-sync debouncer (send `lastPageRead` at most every N s and on exit).
- **RuntimeProfile**: `makeRuntimeProfile(mode, totalMem, usedMem)` → all budgets and limits (see below). Only `Full` is built and used for now (D21).
  Pure and table-tested.
- **Config**: `AppConfig` (server URL, auth mode, credentials/tokens, reader prefs), plus JSON load/save with schema version + migration.

## net
- `HttpClient` interface: `submit(Request, Priority, CancelToken) -> Future<HttpResult>` (D37: `then()` on the net thread, api wraps with `brls::sync`).
- `CurlHttpClient`: one network thread driving a curl *multi* handle, so many transfers share one thread and socket sessions stay low.
  Max concurrent transfers come from `RuntimeProfile` (6 in `Full`). Keep-alive, timeouts, max body size, gzip.
- Auth injection hook (`RequestDecorator`) so `api` can add Basic or Bearer headers and react to 401.
- Priorities: `Interactive` (UI-visible) > `Reader` > `Prefetch` > `Background`. Cancelled jobs are dropped before they run.
- Testing: a fake `HttpClient` for unit tests. A real local HTTP server (cpp-httplib, host-only) for curl-level tests: timeouts, 401, redirects, slow bodies.

## api
- `SuwayomiClient` methods return futures of `core` models: `serverInfo()`, `categories()`, `library(categoryId, page)`,
  `manga(id)`, `chapters(mangaId)`, `fetchChapters(mangaId)`, `pages(chapterId)`, `setProgress(chapterId, page, isRead)`,
  later `sources()`, `search()`, `extensions()`, downloads…
- **Auth**: `none` / `basic_auth` (Basic header) / `ui_login` (`login` mutation → access + refresh JWT; on 401 or expiry call
  `refreshToken`, retry once; on failure go to login screen). `simple_login` (cookie) is supported by
  posting to the login page and keeping the cookie only if needed. Low priority.
- Builds absolute URLs from relative page/thumbnail paths and the server base URL (with optional sub-path).

## image
- Pipeline: `fetch bytes (net)` → `sniff type` → `decode at reduced scale` (libjpeg-turbo DCT scaling where possible) →
  `downscale to target box` → `split into tiles ≤ 2048 px` → `upload texture on main thread`.
- Caches:
  - **Memory**: decoded RGBA, byte-budgeted LRU. The budget comes from `RuntimeProfile`, and it shrinks under memory pressure.
  - **Disk**: compressed bytes on SD under `cache/`, budgeted (default 500 MB), keyed by URL hash.
- Decode runs on a worker thread. Textures are created and destroyed only on the main thread.
- Every load is cancellable, and the reader cancels everything outside its prefetch window.

## ui (borealis)
- Activities: `SetupActivity` (server URL/auth/test connection), `LoginActivity`, `MainActivity` (tab frame: Library, Updates, History, Browse, Settings),
  `MangaActivity` (details + chapters), `ReaderActivity` (paged/webtoon).
- Lists and grids use borealis recycling views. Covers load lazily through `image` with `Interactive` priority.
- **Touch first (D16)**: every action can be reached by touch: tap, drag/flick to scroll, reader tap zones and swipe, pinch zoom (WP-13).
  Touch targets ≥ 72 px. Controllers work everywhere too, because docked mode has no touchscreen.
- Controller (initial): A select, B back, X/Y context actions, L/R page or tab, ZL/ZR zoom, sticks scroll/pan, + menu, − reader settings.
- Reads input from **all connected controllers**, not just player 1 (sys-autopilot's virtual controller can be player 2).

## platform
- `runtimeInfo()` → launch mode + memory, and `showLaunchRefused(text)` for the applet-mode guard (see "Launch modes and runtime profiles" below).
- Paths (`sdmc:/switch/suwayomi-nx/` vs `./data/`), network up/down (nifm), applet focus/suspend/resume hooks,
  memory stats (heap size + `mallinfo` on Switch, D12), battery, and "keep screen on while reading" (disable auto-sleep only inside the reader).

## Threading model
Main thread: borealis loop and all UI/texture work. Net: 1 thread (curl multi). Decode pool: 2 threads (from the profile).
Results come back to the UI with `brls::sync([]{…})`. No locks in UI code. Shared caches use one mutex each, held briefly.

## Launch modes and runtime profiles (D12, amended by D21)

Homebrew on the Switch starts one of two ways. **For now the app supports only application mode** (D21). In applet mode it shows a short
message and exits. Applet support (`Lite`) is a later nice-to-have (WP-21), so this section keeps its design as a reference.

| | **Applet mode** → not supported yet: message + exit (later: profile `Lite`) | **Application mode** → profile `Full` |
|---|---|---|
| How the user gets there | Album icon → hbmenu → our app | Hold **R** while launching any installed game/app → hbmenu → our app (or an hbmenu forwarder) |
| `appletGetAppletType()` | `AppletType_LibraryApplet` (anything except the two on the right) | `AppletType_Application` / `AppletType_SystemApplication` |
| Memory | Much smaller, and smaller still if a game is suspended behind the Album. Atmosphère reserves memory for other applets (`[hbloader] applet_heap_reservation_size`, default 0x8600000) | Nearly all RAM a game would get |
| Network sockets | borealis sets **2** BSD sessions, `sb_efficiency=1` | **12** BSD sessions, `sb_efficiency=8` |

**How it works.** At startup `platform::runtimeInfo()` returns `{mode, totalMem, usedMem}`. On Switch that's `appletGetAppletType()`, the heap size
(`fake_heap_end - fake_heap_start`) and the allocator's in-use bytes (`mallinfo().uordblks`); not `svcGetInfo(UsedMemorySize)`, which counts the
whole reserved heap from the start (D12 amendment, WP-00).
On desktop it's application mode unless the test flag `--simulate-applet-mode` is set, and `--mem-limit=<MB>` caps memory. In applet mode the
guard below runs and the app exits. Otherwise `core::makeRuntimeProfile()` turns that into a `RuntimeProfile`
(pure, unit-tested). Every module reads its limits from the profile instead of from constants. Safe mode after a crash (SAFETY.md S1)
halves the memory budgets and turns prefetch off.

| Setting (initial guesses, tuned in WP-07/12 from real measurements) | Lite (later, WP-21: not built now) | **Full (used now)** |
|---|---|---|
| Decoded-image memory cache | ~25% of free heap at start, capped low (e.g. ≤ 64 MB) | ~40% of free heap (e.g. 256–512 MB) |
| Reader prefetch window | current ±1 page | current −1/+3 pages, plus first 2 pages of next chapter |
| Concurrent HTTP transfers | 2 (matches the socket sessions) | 6 |
| Decode threads | 1 | 2 |
| Max decode resolution | fit-to-screen box, zoom up to 2× by re-decoding the visible region | up to 2× screen (or original if smaller), smoother zoom |
| Webtoon tiles kept decoded | screen ±1 tile | screen ±4 tiles |
| Cover thumbnails | decoded at cell size, small cache | larger cache, prefetch the next row |
| Background prefetch of next chapter | off | on |
| Extra fonts / heavy UI effects | off | on |
| Disk cache (SD) | same (it's on SD, not RAM) | same |

**What the user sees.** In application mode, nothing special. Settings → About shows the current mode and memory.
Opened from the Album (applet mode), the app doesn't start. It shows one plain text screen (exact text in D21): it can't run from the Album yet,
and how to open it by holding R while starting a game. Any button, a tap, or 60 s closes it, and the user is back in hbmenu.

**Applet-mode guard (D21, SAFETY.md S12).** In applet mode any crash is a system fatal, so this path is kept tiny. It runs first in `main()`
(only the logger and the crash handler's set-up start before it), before borealis, GPU, network, images or config. It logs one line
(plus a `launch_refused` test event), then `platform::showLaunchRefused()` draws the text with libnx's text console (Switch) or prints it
(desktop), then `main()` returns normally. If any step fails, it skips the screen and exits.

**Memory-pressure guard.** Every 2 s the app checks used vs total memory. Above 85% of the budget, caches shrink to 50%,
prefetch is paused, and a `mem_pressure` event is logged. The app should degrade (slower page turns), never crash.

## Memory budget (to verify in WP-00/WP-12)
WP-00 measures total/used memory in application mode (applet-mode numbers wait for WP-21).
Those numbers go into STATUS.md and drive the defaults above. The app logs `mem` events every 10 s in test builds,
and the soak test asserts memory stays within the profile's budget.

## Error handling
Network/API errors become a typed `ApiError{kind: Offline|Timeout|Auth|Server|Parse, message}`. Screens show
an inline error with "Retry". Nothing crashes on bad JSON or images. That's covered by fuzz-ish unit tests with corrupt fixtures.
