# Testing strategy

Goal: as much automated testing as possible, with most of it running on the PC in seconds.
The real console is the final check, driven by **sys-autopilot**.

## The six layers

| # | Layer | Runs where | Tool | Speed | Run when |
|---|---|---|---|---|---|
| 1 | Unit | WSL host | doctest + ctest, coverage via gcovr | seconds | every change |
| 2 | Contract | WSL host | Node `graphql` validate: our `.graphql` docs vs `tests/fixtures/schema.graphql` | seconds | every change to queries |
| 3 | Integration | WSL host → Suwayomi test server (Windows :4568) | doctest binary, real HTTP: `tools/build.sh integration` (WP-06) | ~1 min | end of each api/net/image WP, CI |
| 4 | Desktop E2E | WSL (Xvfb headless, or WSLg visible) | Node harness + `DesktopDriver` (xdotool keys, screenshots) | ~2–5 min | every UI WP |
| 5 | Device E2E | Real Switch | Node harness + `SwitchDriver` (sys-autopilot REST) | ~5–15 min | end of every UI WP, before releases |
| 6 | Soak / exploratory | Real Switch | harness soak scenario; Claude via sys-autopilot MCP | 30+ min | WP-12, releases, hard bugs |
| S | **Safety** | build + host + Switch | `check-elf.sh` gate, SafeFs/allowlist unit tests, fuzzing, crash drills (application mode; the one inside the applet-mode guard on desktop only, D41) | gate: seconds, drills ~10 min | gate every build. Drills after WP-03b, before releases, after any crash-layer change |

Safety details are in `docs/SAFETY.md` ("How safety is tested"). One rule applies to **every** device run: a new file in
`atmosphere/fatal_errors/` or `atmosphere/fatal_reports/` is a hard failure. The harness stops and asks the owner. It never retries or continues.
(Atmosphère's "A fatal error occurred" screen writes `fatal_reports/`, unless creport caused it; `fatal_errors/` is written at the next boot
after a secure-monitor panic. Both watched since WP-03, owner OK 2026-10-25.)

Coverage target: ≥ 80% lines for `core/`, ≥ 60% for `net/ api/ image/`. UI is covered by scenarios, not line coverage.

## Test hooks inside the app (the key to cheap, reliable E2E)

Builds with `-DSNX_TEST_HOOKS=ON` (desktop + a test .nro) write **JSON Lines events** to
`<data>/test/events.jsonl` (Switch: `sdmc:/switch/suwayomi-nx/test/events.jsonl`). Examples:

```json
{"t":1234,"ev":"app_start","version":"0.1.0","build":"test","mode":"application","profile":"full","memTotal":0,"memUsed":0,"imageBudget":0}
{"t":1300,"ev":"screen","name":"Library","params":{"category":0}}
{"t":1310,"ev":"focus","id":"manga-cell-3","mangaId":12}
{"t":1500,"ev":"net","op":"library","status":200,"ms":84}
{"t":1700,"ev":"image","url":"/api/v1/manga/12/thumbnail","decodeMs":31,"w":300,"h":450}
{"t":2100,"ev":"reader","chapterId":45,"page":3,"of":20}
{"t":2101,"ev":"idle"}
{"t":9000,"ev":"mem","used":81234567,"total":3335000000,"free":3253765433}
{"t":9500,"ev":"error","kind":"Network","msg":"timeout"}
{"t":9600,"ev":"mem_pressure","usedPct":87,"action":"shrink_caches"}
{"t":40,"ev":"launch_refused","mode":"applet","appletType":2}
```
(`launch_refused` is the only event a run in applet mode writes, D21.)

- WP-03 built `app_start`, `screen` and `idle` (`src/app/TestHooks`, line format and idle rule in `core/TestEvents`). The rest comes
  with WP-08 and the WPs that own them. `app_start` also has `platform`; on desktop `memTotal` is the PC's RAM or `--mem-limit`.
- WP-03b added `app_start.safeMode` / `lastCrash` (the last run crashed: `src/app/crash/README.md`), the `drill` event
  (`{"ev":"drill","name":"null-write","phase":"start"}`, or `"phase":"writing"` for the cache-write drill), and the test config's
  `"drill"` key: that crash drill starts the first time the app is idle (test builds only).
- WP-07 added `image` (one per finished load: `url`, `source` memory/disk/net, `error`, `format`, `w`/`h`, `srcW`/`srcH`,
  `tiles`, `fetchMs`, `decodeMs`, `bytes`, `fileBytes`, `memUsed` at the decode's peak), `mem_pressure` (`usedPct`, `action`
  `shrink_caches` / `recovered`), and the test config's `"imageBench": {"paths": [...], "repeat": n}`, which makes the Hello
  screen run the image benchmark and write one `image_bench` summary (`loads`, `ok`, `failed`, `ms`, `memUsedBefore/After`).
- WP-08 (D40) added `launch_refused` (the only event in applet mode), `mem` (`used`, `total`, `free`, every 10 s), `focus`
  (`view`: the focused view's class and id), `error` (`where`, `kind`, `msg`: a screen shows an error), `server_check` (`where`
  main/setup/diagnostics, `ok`, `version` or `kind`), `login` (the client's login changed: `auth`, `hasAccessToken`,
  `hasRefreshToken`), `login_screen` (`ok`), `config_saved` (`bytes`, `server`, `auth`, `hasToken`), `net_burst` (`sent`, `ok`),
  `screen.params` (Setup: `firstRun`, `url`; Settings: `mode`, `profile`, `memUsed`, `memTotal`, `safeMode`, `crashLogs`) and the
  profile's budgets in `app_start` (`imageBudget`, `coverBudget`, `textureBudget`, `maxDecode`, `httpConcurrency`, `decodeThreads`).
- WP-09 added `library` (the Library tab shows something new: `state` ready/empty/failed, `category` id, `categories` count,
  `sort` title/unread/last_read, `items` loaded, `total` on the server; written again after a refresh), `grid` (the cover grid's
  cells are now items [`first`, `last`), with `columns`; to check that no cover loads for an item that never had a cell) and
  `screen` `Manga` with `params` `id`, `title` (the stand-in manga screen until WP-10).
- `idle` is emitted when there are no pending net/decode jobs (`http.inFlight()`, `images.activeJobs()`, WP-08) and no running animations (borealis's running tickings) for 3 frames in a row,
  once per quiet spell: again only after something was busy or a new `screen` came. **Tests wait for `idle`, never `sleep`.**
- The app empties the file at start and appends each event with open → write → close, never keeping it open: on the Switch a file that one
  program has open for writing can't be opened by another, so a line the app can't write right now (sys-autopilot is reading the file)
  stays in a queue and goes out on the next frame. Since WP-08 the logger writes `logs/app.log` the same way (every 2 s), so it can be read while the app runs.
- Test config `<data>/test/config.json` (uploaded by the harness, `core/TestConfig.hpp`): `"server": {"url", "auth":
  "none|basic|ui_login", "username", "password"}` = the app starts with that server instead of config.json's (no setup screen);
  `"showSetup": true` = the setup screen opens with those values filled in (the Switch can't type in a test; the login screen
  fills in the password too); `"diagnostics": {"burst": n}` = the diagnostics screen; `"imageBench"`; `"drill"`. The harness
  removes `config.json` before every launch unless `launch({ keepConfig: true })` (a restart test).
- Events are text. Asserting on them costs almost no tokens, while screenshots are images and cost a lot.

## Fixture manga (deterministic content)

Generated by `tools/gen-fixtures` (Node + sharp) into the test server's **Local Source** folder. Suwayomi reads local
folders/CBZ without needing extensions or internet, so tests are fully offline. Fixtures are synthetic,
with no copyrighted pages. Each page shows big text ("ALPHA / CH 2 / P 5/10") **and a 4×4 colour-code block** in its top-right
corner that encodes (series, chapter, page, segment) with a checksum (D31). The harness reads that block from a screenshot with
`findPageCodes()` / `readPageCode()` in `tests/e2e/lib/pagecode.ts` to know exactly which page is on screen, with no OCR and no model
looking at images. It still reads correctly after the page is scaled into a 1280×720 JPEG screenshot (unit-tested).
The set is defined in `tools/gen-fixtures/fixtures.ts` (tests import it for expected values); same bytes on every run.

Fixture set (series code in brackets = the colour code's series number):
- `Alpha` [1] – 3 chapters × 10 pages, JPEG 1000×1500, with `details.json` (author, genres, ongoing) and `cover.jpg`
- `Beta RTL` [2] – 2 chapters × 8 pages, PNG 1000×1500; chapter 1 page 4 is a double-page spread (2000×1500)
- `Gamma Webtoon` [3] – 2 chapters × 3 tall strips 800×12000 WebP. A colour code every 1500 px, with the segment number (0–7)
- `Delta Big` [4] – 300 chapters × 2 pages, JPEG 600×900 (pagination, long lists)
- `日本語タイトル テスト` [5] – Unicode title, 1 chapter × 5 pages, no `cover.jpg`: the server copies its first page in as the cover
  (Local Source does that for every folder without one, so a manga with no cover at all can't exist)
- `Broken` [6] – 1 chapter × 5 pages: page 1 corrupt JPEG, page 2 zero bytes, page 3 a 1×1 image, pages 4–5 fine. Page 1 is also
  its (broken) cover, which makes it the **fallback-art** case. The server lists all 5 pages.

- The **Big shelf** [7] (WP-09, `SHELF` in `fixtures.ts`) – 150 small manga `Shelf 001` … `Shelf 150`, 1 chapter × 1 JPEG page
  400×600 each (that page is also the cover), in light colours that differ from cover to cover. They give the library grid
  something to scroll, page (60 per request: 60 + 60 + 30) and recycle. They all share code 7: they never test the reader.
  The server numbers manga in folder order, so these take ids 6–155 and `日本語タイトル テスト` is id 156.

After seeding (`tools/test-server/seed.ts`): categories `Reading` (Alpha, Beta RTL), `Long series` (Gamma Webtoon, Delta Big),
`Big shelf` (the 150) and `Empty` (none: the "nothing in this category" screen); `Broken` and `日本語タイトル テスト` are in no
category (the Default category, id 0). Real answers for these are in `tests/fixtures/responses/`.

## Drivers (one scenario, two drivers)

`tests/e2e/drivers/Driver.ts` (how to write a scenario: `tests/e2e/README.md`):

```ts
interface Driver {
  deploy(buildPath: string): Promise<void>;        // desktop: checks the build folder; switch: PUT /files + verify sha256 (skipped if equal)
  launch(opts?: {mode?, testConfig?, memLimitMb?, args?}): Promise<void>;   // waits for app_start
  quit(): Promise<void>;                           // desktop: SIGTERM, exit code 0; switch: PLUS, no new crash report
  press(btn: Button, ms?: number): Promise<void>;
  sequence(btns: Button[]): Promise<void>;
  stick(side: 'left'|'right', x: number, y: number, ms: number): Promise<void>;
  touch(x: number, y: number, ms?: number): Promise<void>;   // one finger, screen pixels 0-1279 × 0-719, tap = default 100 ms
  swipe(x1: number, y1: number, x2: number, y2: number, ms: number, steps?: number): Promise<void>;
  typeText(text: string): Promise<void>;           // WP-08: desktop types into the open text dialog; Switch: NotSupportedError
  screenshot(): Promise<Buffer>;                   // 1280×720: PNG on desktop (lossless), JPEG on the Switch
  requestExit(): Promise<void>;                    // WP-03b: desktop SIGTERM, Switch HOME → Close software (an exit request)
  waitForExit(timeoutMs: number): Promise<string>; // WP-03b: the app ended by itself (crash drills); Switch: crash folders checked
  listFiles(dir: string): Promise<string[]>;       // WP-03b: names in a folder of the data folder, e.g. "logs"
  readFile(path: string, offset?: number): Promise<Buffer>;   // paths inside the app's data folder (D10), e.g. "logs/app.log"
  writeFile(path: string, data: Buffer|string): Promise<void>;
  events(): EventStream;                           // tails test/events.jsonl of the current launch
  dispose(): Promise<void>;                        // end of scenario: stop the app, back to the start state
}
```

- **DesktopDriver**: launches the desktop build under Xvfb at 1280×720 (or WSLg when you want to watch),
  sends keys with `xdotool` (borealis maps keyboard to gamepad on desktop), screenshots with ImageMagick `import`, and reads files directly.
  Touch (D16): `xdotool` mouse clicks and drags act as one finger. Multi-finger gestures (pinch) go through a test-hook command that
  injects touch events straight into the app.
  Checked in WP-00 with the borealis demo (Xvfb, and the owner's mouse in WSLg):
  - Start it from its build folder (it loads `./resources`). Under Xvfb set `XDG_SESSION_TYPE=x11` and unset `WAYLAND_DISPLAY`: GLFW tries
    Wayland first and otherwise opens on the WSLg screen even with `DISPLAY=:99`. Mesa renders in software there, which is fine at 1280×720.
  - A **tap** is `mousedown`, 100 ms, `mouseup`. `xdotool click` is too quick: borealis reads the mouse once a frame and misses it.
    A **drag** is `mousedown`, moves every ~16 ms, `mouseup`. Tap opened a tab, drag scrolled a recycling list (with momentum), arrow keys moved the focus.
  - Built in WP-03: Xvfb on the first free display from `:90` (under WSLg `/tmp/.X11-unix` is read-only, so Xvfb only has its abstract
    socket; "ready" = `xdotool getdisplaygeometry` answers). The window is found by the app's process id and gets the focus (borealis ignores
    input without it). Desktop borealis maps only **Enter = A, Escape = B, middle mouse = X, arrow keys = D-pad**; other buttons and sticks
    throw `NotSupportedError`. There is no key for PLUS, so `quit()` sends SIGTERM, which the app turns into a normal quit (exit code 0).
    The app's stdout goes to `out/<scenario>/stdout.txt`.
- **SwitchDriver**: talks to our sys-autopilot-nx (D19, `tools/sys-autopilot-nx/README.md`) at `http://<switch-ip>:4150`, the IP from
  `tests/e2e/device/.env` together with the Bearer token. Use the IP: `switch-dev.local` resolves on Windows but not inside WSL. Without
  the token every request gets 401, and removed upstream routes get 404.
  - deploy: `PUT /files?path=/switch/suwayomi-nx/suwayomi-nx.nro`, then `GET /files/hash` and compare SHA-256. The PUT writes in place
    (not atomic) and deletes the file if the upload breaks off, so the hash check is required, and on a mismatch upload again
  - input: `POST /input/tap|hold|release|stick|clear`
  - touch (checked on the console in WP-00b, handheld): `POST /input/touch {"x","y","durationMs"?}` = tap (100 ms by default) or hold;
    `POST /input/swipe {"x1","y1","x2","y2","durationMs","steps"?}`. Screen pixels, origin top-left, x 0–1279, y 0–719, plain integers.
    Anything else gets 400 with a message. Durations ≥ 1 ms, capped at 10 s. The call returns after the finger is lifted, and real touches work
    again afterwards. `/input/clear` also releases touch. Touch scenarios run handheld (the touch screen is off when docked; whether injected
    touch reaches apps while docked is not tested yet)
  - screenshot: `GET /screenshot` (JPEG 1280×720, ~190 KB, ~0.2–0.6 s). During an app switch it fails (HTTP 500, rc `0xe0ce`): retry,
    and accept only HTTP 200 + JPEG data
  - **screen checks** (D24): before any press that could start something, crop the screenshot and compare it with a reference crop
    (ImageMagick RMSE < 0.08). Regions and references: `tests/e2e/device/profile.json`, crops in git-ignored `tests/e2e/device/local/`.
    Text-free, cheap, and no model looks at the image
  - events: `GET /files?path=…/test/events.jsonl&offset=<bytes read so far>`, polled every 500 ms. Whether the app is still there is
    checked every 3 s with a screenshot (hbmenu, the HOME menu, or its "The software was closed because an error occurred." note
    on screen = it's gone; the driver dismisses that note with A after the screen check, WP-03c)
  - **requests (WP-03, D32):** one at a time, at least 150 ms apart, a fresh connection each that the PC closes once the answer is complete.
    Bursts of connections (~50 in a second) made the console reset every new one, curl's too, for ~10 s. Reads, uploads and deletes that hit a
    reset back off (0.5 → 30 s, about a minute in all since WP-03c) and try again; `/input` and `/controller` calls never do (a repeated A could start a game)
  - crash check: list `atmosphere/crash_reports/`, `atmosphere/fatal_errors/` and `atmosphere/fatal_reports/` at launch and after quit/cleanup
  - status: `GET /status` (version, firmware, controller attached, uptime, battery, `screenshots` available, `heap` size/peak/used)
  - the virtual Pro Controller is released before sleep and comes back on the next button call (`controllerAttached` goes false after a wake)
  - launch: get to hbmenu (sequence calibrated in WP-00 and stored in `tests/e2e/device/profile.json`), then either
    (a) hbmenu netloader: press **Y**, run `nxlink -a <ip> -p suwayomi-nx/suwayomi-nx.nro --args "--test" app.nro` (preferred, D24;
    `-p` is relative to `sdmc:/switch/`, ~1 s for 4.5 MB, app on screen ~2 s after nxlink starts), or
    (b) select the uploaded .nro in hbmenu's list and press **A** (fallback: the position depends on what else is on the card).
    hbmenu lists `sdmc:/switch/X/` as an app if it holds `X.nro` or exactly one `.nro`, so D10's `suwayomi-nx/suwayomi-nx.nro` shows as the app.
  - quit: PLUS → hbmenu again (~1.3 s). Read the app's log afterwards with `GET /files` (nxlink `-s` can't reach WSL through the NAT).
  - recover: HOME → X → wait for the "Close the software…?" dialog (screen check) → A → back to known state (first HOME tile selected).
    If a fatal error screen is detected, stop and ask the owner to press POWER. WP-03 detects it two ways: a new fatal file (above), or the
    console no longer answering at all (`ConsoleUnreachableError`, whose message asks the owner to look at the screen and press POWER if it
    shows the fatal screen). A new fatal file also writes `tests/e2e/out/STOP-console-fatal.txt`, and the SwitchDriver refuses to start
    while that file exists, so nothing retries until the owner has checked the console and deleted it.
  - **safety allowlist**: the driver can only call the endpoints listed in SAFETY.md S10. Anything else throws before sending.

## Launch mode (application mode only, D21)

Every scenario runs in **application mode** with the `Full` profile. Applet mode isn't supported yet: the app shows a short
"open it by holding R" screen and exits. The `applet-guard` scenario is the only one that launches in applet mode.

| Driver | Every scenario (application mode, `Full`) | `applet-guard` only (applet mode) |
|---|---|---|
| Desktop | default. Add `--mem-limit=<MB>` for low-memory runs | `--simulate-applet-mode` |
| Switch | HOME → first tile (Spelunky 2) → **hold R** (`POST /input/hold {"buttons":["R"]}`) + A → A on the user picker → hbmenu → release R → app | HOME → Album → hbmenu ("Applet Mode" badge) → app |

- `app_start` reports `mode`/`profile`. The first assertion of every scenario is that it says `application`/`full`.
- `tools/e2e.sh <driver> <scenario>` always launches in application mode. Low memory is tested on desktop with `--mem-limit`.
- Applet mode comes back only with WP-21 (nice to have). Then this section gets the two-profile matrix from D12 back.

## Scenarios (written once, run on both drivers)

`tests/e2e/scenarios/*.test.ts` using vitest. Each one: launch with test config → steps → assert events (+ optional golden diff) → quit.
Initial list, grown per UI WP:
1. `smoke` – app starts with the test server, reaches Library connected (`server_check`), `idle`, golden, Settings by touch, no `error` events, no crash reports (WP-08)
2. `setup-first-run` – no config → Setup screen → (desktop: tap the field, type the URL; Switch: `showSetup`) → Continue → Library; a restart skips setup (WP-08)
3. `auth-matrix` – none / basic (+ wrong password → Setup) / ui_login (login, renewal after the 20 s JWT, logout → login screen, restart on saved tokens), restarting the test server per mode (WP-08)
4. `library-browse` – Default's covers (golden), tap the Big shelf tab (golden), tap a cover → Manga → B back, D-pad to the last row (pages load 60 → 120 → 150) and back, two more passes by flicking; memory flat over the 3 passes (`mem`), no cover downloaded twice, no cover loaded for an item that never had a cell (`image` vs `grid`); X refresh; Sort → Unread by touch; the Empty tab (golden); a restart keeps category and sort (WP-09). The 300-chapter list is WP-10's.
5. `reader-paged` – open Alpha ch1, flip 10 pages, colour-code confirms each page, progress saved on server (checked via GraphQL from the harness)
6. `reader-rtl-spread` – Beta RTL, direction and spread handling
7. `resume-progress` – reopen chapter, lands on last page
8. `offline-errors` – stop test server mid-session → error UI + retry works after restart
9. `broken-images` – Broken fixture shows placeholders, no crash
10. `soak` (device only, application mode) – read 500 pages across chapters, `mem` events stay within the profile's budget, no leaks/crashes
11. `applet-guard` (desktop `--simulate-applet-mode`, device via the Album) – the "can't run from the Album yet" screen shows, `launch_refused`
    is the only event, a button press or tap closes it and the app is back in hbmenu, no new crash report or fatal file. Desktop also runs
    the null-pointer drill inside that screen (SAFETY.md S12). The console: opt-in `SNX_APPLET_CONSOLE=1` with the owner watching, the
    refusal screen only (golden `refused`), never the drill: from the Album any crash is a system fatal (D41). Green on both in WP-08.
12. `low-memory` (desktop) – a `--mem-limit` run triggers `mem_pressure` without a crash, and About shows the mode and memory. Green since WP-08.
13. `crash-drills` (device and desktop, application mode) – every drill in SAFETY.md ends in a clean exit + crash log + safe mode next launch,
    with zero system fatals. One test per drill (`crash-drill-<name>`); the drills and what each should end in: `src/app/crash/README.md`.
    Desktop green since WP-03b; the console twice in a row since WP-03c (every crash there ends with the HOME menu's
    "software was closed" note, D35)
14. `net-hello` (device and desktop, WP-05) – the diagnostics screen (WP-08; was the Hello screen) asks the test server for its version through the curl client, then sends 20
    requests at once; all 21 `net` events must be HTTP 200 (the 20 queue behind 6 transfers, none fails). Saves a screenshot to look at.
    Needs the test server; the Switch reaches it at the PC's LAN address (`lib/suwayomi.ts` `appServerUrl`). Setup's "Test connection" is the user-facing version check.

## Golden screenshots
- Stored per driver: `tests/e2e/goldens/{desktop,switch}/<scenario>/<step>.png`.
- Compared with `pixelmatch` (per-pixel threshold 0.1, allowed diff 0.1 % of the pixels by default), with masks for clock/battery/animated
  regions. Masks are painted magenta into both pictures, and into the stored golden, so the PNG shows what is ignored.
- Update with `tools/e2e.sh <driver> <scenario> --update-goldens` only when a UI change is intended (and say so in the commit).
- On failure, the harness writes `<step>.actual.png`, `<step>.diff.png`, the events and the app log to `tests/e2e/out/<scenario>/`.
  Claude opens the diff image only then.

## Using sys-autopilot interactively (exploration/debug sessions)
Add it as an MCP server so Claude can look and press buttons directly:
`claude mcp add --transport http switch http://<switch-ip>:4150/mcp --header "Authorization: Bearer <token>"` (SETUP.md D; the token is required).
It offers `touch` and `swipe` next to the button, screenshot and file tools.
Use this for exploring and debugging. Automated tests use the REST API through the harness, which is cheaper
and repeatable. Each MCP screenshot is an image in context, so use them sparingly.

## CI (GitHub Actions, once a repo exists)
1. `host`: build + unit tests + coverage (ubuntu)
2. `contract`: graphql validate, runs in the `node` job (`npm run contract`, WP-04). Plus `fuzz`: libFuzzer + ASan/UBSan, 60 s per target (WP-04, SAFETY.md S6)
3. `integration`: Suwayomi container (`ghcr.io/suwayomi/suwayomi-server:<pinned>`) + fixtures + integration tests
4. `desktop-e2e`: Xvfb + scenarios (goldens: desktop)
5. `switch-build`: `devkitpro/devkita64` container builds release + test .nro, uploads artifacts
Device E2E runs locally (`tools/device-test`). Optional later: a self-hosted runner on this PC (WP-20).

## Definition of done for any WP
- Layer 1–2 green. Layer 3 green if the WP touched net/api/image. Layer 4 green if it touched UI.
- Layer 5 run if the WP touched UI, platform or memory, or if it's a release.
- New logic has unit tests. New screen has at least one scenario. Bugs get a regression test first.
