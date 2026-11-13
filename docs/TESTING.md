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
  `hasRefreshToken`), `login_screen` (`ok`), `config_saved` (`bytes`, `ms`, `server`, `auth`, `hasToken`), `net_burst` (`sent`, `ok`),
  `screen.params` (Setup: `firstRun`, `url`; Settings: `mode`, `profile`, `memUsed`, `memTotal`, `safeMode`, `crashLogs`) and the
  profile's budgets in `app_start` (`imageBudget`, `coverBudget`, `textureBudget`, `maxDecode`, `httpConcurrency`, `decodeThreads`).
- WP-09 added `library` (the Library tab shows something new: `state` ready/empty/failed, `category` id, `categories` count,
  `sort` title/unread/last_read, `items` loaded, `total` on the server; written again after a refresh), `grid` (the cover grid's
  cells are now items [`first`, `last`), with `columns`; to check that no cover loads for an item that never had a cell) and
  `screen` `Manga` with `params` `id`, `title` (the stand-in manga screen until WP-10).
- WP-10 added `manga` (the details screen shows something new, not while loading or busy: `state`, `id`, `items` shown,
  `loaded`, `total`, `complete`, `sort` newest/oldest, `filter` all/unread/bookmarked, `first` = sourceOrder of the top row,
  `unread`, `continue` {`kind` none/start/continue/all_read, `chapter` id, `page`}, `description` short/closed/open),
  `chapter_action` (`action` mark_read/mark_unread/bookmark/remove_bookmark/mark_previous_read/mark_previous_unread/refresh,
  `chapter` id, `ok`; `changed: false` when there was nothing to change), `chapter_list` (rows [`first`, `last`) have
  cells), `screen` `Manga` again whenever it is back on top, and `screen` `Reader` with `mangaId`, `chapterId`, `page`
  (the stand-in until WP-11). Chapter rows have the ids `manga/chapter-<index>`.
- WP-11 (D44) added `reader` (what the reader shows, once it has settled; nothing while the page or chapter is loading:
  `chapterId`, `chapter` = sourceOrder, `page` = index, -1 for the "previous chapter" screen and `of` for the "next chapter"
  screen, `of`, `shown` page/page_error/chapter_error/start/end, `direction` ltr/rtl, `fit` screen/width/height, `menu`,
  `pageNumber`, `background`, `read`; a page: `spread`, `w`/`h` drawn, `srcW`/`srcH`; an error: `error` kind; `retries`;
  after a key, tap or swipe: `turnMs` = from the input to the frame that shows the result, `ready` = the page was decoded
  already then; `memUsed`), `progress` (every progress save: `chapterId`, `page`, `read`, `ok`; also the ones that end after
  the reader closed), `error` with `where` reader (a page or chapter that couldn't be loaded), and `screen` `Reader` again
  when it is back on top (`params`: `mangaId`, `chapterId`, `page`). Reader touch coordinates: `READER` in `lib/screens.ts`;
  shared steps (open a chapter, wait for a `reader` event, read the colour code, change a setting): `lib/reader.ts`.
- WP-13 (D48) added to `reader` (always) `zoomOnTurn` reset/keep and `texUsed` (the GPU texture budget in use, bytes), and for a
  page `zoom` (1 = the fit, 2 decimals), `x`/`y` (the page corner on the reader screen; a zoom or a finished move reports
  again), `decW`/`decH` (the decoded image drawn), `sharp` (it has the detail the zoom on screen wants, as far as the source
  and the cap allow) and once `sharpMs` (from the last zoom change to the frame that draws the sharper decode, the 150 ms
  settle wait included; WP-13c adds `uploadMs`, that decode's texture upload); WP-13c: (always) `splitSpreads`
  portrait/always/never, for a page `split` (it shows as two halves) and `half` (0 = the first in reading order; `w`/`h`
  are the half's then); `gesture` after every pinch, drag, glide (D-pad), stick pan, zoom step and double tap (`kind`,
  `frames`, `p90Ms`/`maxMs` = frame intervals while it ran, without the time the test hooks themselves spent on the SD
  card in them (writing events, the touch channel: a release build doesn't do that; WP-13b), `zoom`, `x`, `y`);
  `command` (a played touch command is done:
  `id`, `ok`, `error`). The reader also counts as busy for `idle` while it settles by itself (a middle tap waiting for a
  second one, the zoom about to decode, a glide, a pinch, a stick push).
- WP-13b (D49): in the webtoon strip, `reader` has `mode` "strip" (the page reader's say `mode` "paged"), `chapterId` and
  `chapter` of the chapter being read (the one under the middle line), `page` (0-based) and `of`, `shown` strip or
  chapter_error, `spot` (the middle line's share of that page, 4 decimals), `top` (the strip's y at the screen's top, whole
  units), `stripW` (units) and `stripWidth` half/three-quarters/full, `auto` (genre/shape: the rule that picked the strip by
  itself, only then), `direction` vertical, `read`, `chapters` (the chapter ids in the strip, top to bottom), `pagesHeld`,
  `tiles` (textures kept), `waitFrames` (frames since the last event that showed a page or tile still loading), once
  `openMs` (the reader opening to the first settled strip), `retries`, `texUsed`, `memUsed`, and the common menu,
  orientation and screen fields. It writes once the strip has settled somewhere new (no finger, flick, glide or held scroll
  under way, nothing on screen loading) and on every chapter change. `progress` adds `spot` in the strip; `gesture` adds the
  kinds `fling`, `glide` and `scroll` (held D-pad, sticks) and `top`. The strip counts as busy for `idle` while it moves or a
  page on screen loads.
- WP-24 (D54): `library` gains `search` (the search text, or null) and `titles` (the first 10 titles shown); `categories`
  (the Categories screen shows something new: `state` loading/ready/empty/failed, `rows` [{`id`, `name`, `count`}] without
  Default, `changed`); `category_action` (`action` add/rename/move/delete, `ok`, `id`, or `problem` empty/too_long/taken
  when the app refused the name without asking the server); `category_picker` (`state` ready/failed/saving/saved, `manga`,
  `rows` [{`id`, `name`, `checked`}], once saved `saved` = the ids and `sent` = whether a request went); `dialog` (`text` of
  a question, e.g. a delete); `screen` `Categories` and `CategoryPicker` (`params.id` = the manga).
- `idle` is emitted when there are no pending net/decode jobs (`http.inFlight()`, `images.activeJobs()`, WP-08) and no running animations (borealis's running tickings) for 3 frames in a row,
  once per quiet spell: again only after something was busy, a new `screen` came, or (WP-10) any other event a screen
  writes (`library`, `manga`, `chapter_action`, ...: on the console an answer can arrive within one frame, so the app never
  looked busy and a wait for `idle` after the event waited in vain). **Tests wait for `idle`, never `sleep`.**
  Busy for 5 s on end: one `busy` event (`ms`, `tickings` = running animations, `jobs` = pending net/decode work), so a
  wait for `idle` that times out shows what kept the app busy.
- The app empties the file at start and appends each event with open → write → close, never keeping it open: on the Switch a file that one
  program has open for writing can't be opened by another, so a line the app can't write right now (sys-autopilot is reading the file)
  stays in a queue and goes out on the next frame. Since WP-08 the logger writes `logs/app.log` the same way (every 2 s), so it can be read while the app runs.
- Test config `<data>/test/config.json` (uploaded by the harness, `core/TestConfig.hpp`): `"server": {"url", "auth":
  "none|basic|ui_login", "username", "password"}` = the app starts with that server instead of config.json's (no setup screen);
  `"showSetup": true` = the setup screen opens with those values filled in (the Switch can't type in a test; the login screen
  fills in the password too); `"diagnostics": {"burst": n}` = the diagnostics screen; `"imageBench"`; `"drill"`; `"maxDecodeScale": 1..4` = decode up to that many times the screen instead of the profile's value (WP-13c, for
  the measurement in `reader-spread-split`); `"diskCache": false` = the image
  loader runs without its SD-card cache (WP-12: pages cached by earlier runs would load with the server stopped). The harness
  removes `config.json` before every launch unless `launch({ keepConfig: true })` (a restart test). On the Switch the
  driver copies the owner's `config.json` and `.bak` first and puts them back exactly after every scenario (SwitchDriver, below).
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
- `Alpha` [1] – 3 chapters × 10 pages, JPEG 1000×1500, with `details.json` (author, genres, ongoing) and `cover.jpg`;
  its description is long on purpose (the details screen's "Show more", WP-10)
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
- `Epsilon Weekly` [8] (WP-15) – 2 chapters × 3 pages, JPEG 1000×1500, `details.json` (ongoing). For the Updates tab's "a library
  update found a new chapter". It is generated into `.test-server/fixtures-extra/`, **not** into the pristine folder that `reset`
  copies (the server numbers manga in folder order, so Epsilon there would shift every later id). The seed adds it as a second
  step: manga 157, chapters 460–461, in Reading after Alpha and Beta RTL, both chapters read (`isRead` only, so History stays
  empty). A held-back **Chapter 3** (3 pages) is generated too: `addNewChapter()` (tests/e2e/lib/suwayomi.ts) copies it into the
  server's `local/Epsilon Weekly/` with a fixed folder time; after `updateLibrary(input: {})` the server has chapter 462.
  `removeNewChapter()` deletes it and re-fetches the chapter list, so the server forgets it. Both are safe to call twice.

After seeding (`tools/test-server/seed.ts`): categories `Reading` (Alpha, Beta RTL, Epsilon Weekly), `Long series` (Gamma Webtoon, Delta Big),
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
  Touch (D16): `xdotool` mouse clicks and drags act as one finger. Multi-finger gestures (pinch) go through the touch channel below.
- **Touch channel (WP-13, both drivers, test builds only):** the drivers have one finger (sys-autopilot-nx also needs 150 ms
  between requests), so pinches and double taps are played inside the app. The harness writes `test/commands/<n>.json`
  (`n` = 1, 2, ... in every launch, ≤ 64 KiB): `{"cmd":"touches","frames":[{"ms":0,"fingers":[{"id":0,"x":590,"y":360},
  {"id":1,"x":690,"y":360}]}, ...]}` in landscape screen units. About every 100 ms the app looks for the next number, plays
  the frames (one per app frame once its `ms` is due; a finger down is touched every frame, a finger missing from the next
  frame lifts, all lift after the last), deletes the file and writes `{"ev":"command","id":n,"ok":true}`. Each finger gets
  the same hit test and gesture recognizers as a real one. A file that doesn't parse yet is read again (50 tries). Helpers:
  `pinch()`, `doubleTap()`, `drag()`, `flick()` (lifted while moving, WP-13b) and `playTouches()` in
  `tests/e2e/lib/touches.ts` (`map` = `portraitPoint` for portrait). The strip's page codes: `codesOnScreen()` (every code
  on screen with its place, the screenshot turned upright) and `middleCode()` (the one nearest the middle line) in
  `lib/reader.ts`. The real two-finger path is checked by the owner's hands on the console.
- **Text command (WP-24, both drivers, test builds only):** the Switch's system keyboard can't be typed into, so
  `{"cmd":"text","text":"Test shelf"}` on the same channel (same numbering) stores a text, and the app's next keyboard request
  (search, a category name: `askText`) takes it instead of opening the keyboard. Send it before the tap that asks:
  `typedText(s, text)` in `lib/touches.ts`. The real keyboard is checked once on desktop (`typeText` + `KEYBOARD_OK`).
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
  - **the owner's settings** (found 2026-10-30: every run wiped them): the console is also the owner's reading console (D18), and
    its `config.json` names the owner's real server. Before the first launch changes it, `drivers/ownerConfig.ts` copies
    `config.json` and `config.json.bak`, and the waiting progress saves `progress-queue.json` + `.bak` (WP-15b: a test run never
    loses them nor sends them to the test server), to `tests/e2e/out/owner-config/` (git-ignored, hash-checked, `pending.json`
    written last; an older `pending.json` without the queue files leaves them as they are).
    Every scenario's cleanup puts them back (upload + SHA-256, or delete where there was no file), removes the harness's
    `test/config.json`, and renames `pending.json` to `restored.json`. `tools/e2e.sh switch` restores once more after the run,
    also after a failure or Ctrl+C; `tools/e2e.sh switch --restore-config` does only that. A leftover `pending.json` is never
    overwritten (it holds the owner's files, the console the tests'). After a new fatal file the copy waits for the owner.

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
4. `library-browse` – Default's covers (golden), tap the Big shelf tab (golden; since WP-24 the Empty tab is reached by tapping Default, RIGHT ×4, A), tap a cover → Manga → B back, D-pad to the last row (pages load 60 → 120 → 150) and back, two more passes by flicking; memory flat over the 3 passes (`mem`), no cover downloaded twice, no cover loaded for an item that never had a cell (`image` vs `grid`); X refresh; Sort → Unread by touch; the Empty tab (golden); a restart keeps category and sort (WP-09). The 300-chapter list is WP-10's.
4b. `manga-details` – Alpha: details + the long description open/closed (goldens), tap a chapter → the reader at the right chapter/page (a stand-in until WP-11), X read/unread, a row's "..." menu read/unread/bookmark, Show bookmarked, each checked on the server via GraphQL; Delta Big: 300 chapters load, Sort oldest, "Mark all before as read" on ch 4 = exactly 1–3 on the server, Show unread (golden), Continue → ch 4, progress saved on the server shows after Refresh and Continue opens that page; Switch: Y and MINUS menus (WP-10)
4c. `library-tools` (WP-24, D54) – search "alp" → Alpha in the whole library (golden `search`; desktop types into the real dialog once, otherwise the text command), "zzz" → the search-empty message, B ends the search; Edit → Categories (golden `categories`): Add "Test shelf", " reading " refused as taken, Rename, Move up, Delete after its question (golden `dialog`), the server back at the seed; Alpha → Categories: Reading ticked, tick Long series, Save (golden `picker`), the server has both; 10 rounds Categories open / close (memory + < 16 MiB). Switch: Y on a cover → Categories... → untick → Save, MINUS → Search..., and the times (search ≤ 1.5 s, picker ≤ 1 s, actions ≤ 1 s). `lib/categories.ts` puts the seed back at the end, also after a failure.
5. `reader-paged` – Alpha ch1 (goldens page, menu, end; WP-13: it first waits for the launch's config save): left to right through the settings menu, pages 2–10 by D-pad, taps on the right edge (one slides 20 units: still a tap, D46) and swipes, the colour code confirms each page; the last page marks ch1 read on the server (GraphQL); the "next chapter" screen, ch2 at once (prefetched), back to ch1's last page; fit width (1280 × 1920); 100 page turns with memory flat (growth < 16 MiB after turn 20) and the prefetched turn time (console: 90th percentile < 150 ms, D17); B → the details screen shows the progress; a restart keeps Alpha's direction and fit (WP-11)
6. `reader-rtl-spread` – Beta RTL ch1, right to left (the default): LEFT, a tap on the left edge and a swipe to the right go forward, RIGHT and the right edge back; page 4 is reported as a spread and shown whole at 960 × 720 (golden); the "next chapter" screen (WP-11)
6b. `reader-portrait` (WP-22, D46) – Alpha ch1 turned to portrait: Y (desktop: the menu's Rotate, the desktop can't press Y
    or L) gives turned left, the page 720 × 1080 on the 720 × 1280 reader screen; a tap on the left edge, the turned D-pad (UP
    points left) and a swipe turn the right way; the menu (golden); Rotate by touch back to landscape; fit height and the
    settings' Orientation → turned right (goldens menu, page); a drag moves the 853 × 1280 page (event `x` -133 → 0, golden),
    the same drag then turns; DOWN points left; the end screen both ways (goldens); the menu's Close by touch; a restart keeps
    the orientation, then 100 turns with the turned D-pad (memory growth < 16 MiB after turn 20, prefetched turns p90 < 150 ms
    on the console, D17); desktop `--simulate-docked` reads landscape and keeps the saved choice. Touch points are written on the
    portrait screen and turned with `portraitPoint` (`lib/screens.ts`). After each orientation change it waits for the
    config save: the save holds the main loop for a moment and a 100 ms desktop tap during it is lost. ~35 s on desktop
6c. `reader-zoom` (WP-13, D48) – Alpha ch1, landscape: a played double tap in the middle → zoom 2 around the tapped point (x 160,
    y -200), then the sharper decode (`decW` 1000, `sharp`, golden `zoom2`); a drag down stops at the top (y 0); a played pinch
    to 3 × (x -80, y -180); a drag right stops at the left edge (x 0), the same drag turns (page 2 at the fit); a pinch in and
    out snaps to the fit; double tap in and out; a single middle tap opens the menu after the 250 ms wait; an edge tap turns at
    once (`turnMs` < 100 on desktop) with `texUsed` back at the fit page's; "Zoom on page turn: Keep" → a turn keeps 2 × at
    the top, and survives a restart; turned right: the same double tap, pinch (golden `portrait-zoom3`, before the drag: the
    console's swipe varies by a step, WP-13b) and drag on the portrait screen. The Switch adds (green 2026-11-07, ~7 min):
    ZR/ZL steps, a stick pan and a fresh push at the edge, D-pad glides and the edge press, a held stick and a held portrait D-pad turn one page, 50 zoom cycles (memory growth < 16 MiB
    after the fifth, `texUsed` back to the fit), 100 turns from zoomed pages (p90 < 150 ms), and the gesture frame times
    (p90 ≤ 17 ms, none over 34 ms) and `sharpMs` p90 ≤ 350 ms (console: frames p90 17 / max 17 ms, `sharpMs` p90
    183 ms, zoomed turns p90 18 ms, memory growth 0). ~20 s on desktop
6e. `reader-spread-split` (WP-13c, D48) – Beta RTL ch1 turned left ("Split spreads: In portrait"): page 4 shows its right
    half first (`split`, `half` 0, 720 × 1080, `sharp`, decoded 1440 wide; its page code, read from the screenshot turned
    upright; golden `split-right-half`), a played double tap zooms that half and back, the next step its left half (no page
    code; golden `split-left-half`), then page 5; back lands on the left half, then the right; after B the server has
    `lastPageRead` 3; Continue opens page 4 at its first half; Rotate → landscape shows it whole (960 × 720); "Always"
    splits it there (480 × 720, golden `always-landscape-right-half`); left to right starts with the left half; "Never"
    in portrait shows it whole (720 × 540). The Switch adds the `maxDecodeScale` measurement: ZR to 4 × on the split half,
    logging the decoded size, `sharpMs`, `uploadMs`, the zoom steps' frames, memory and `texUsed`; run it at another cap
    with `SNX_MAX_DECODE_SCALE=2` (the test config's `"maxDecodeScale"`; the profile has 4, D48). ~30 s on desktop
6f. `reader-webtoon` (WP-13b, D49; zoom WP-13d, D50) – Gamma Webtoon ch1 opens as a strip by itself (`auto` shape), turned left on a fresh
    config, 720 wide, its first page's top at the screen's top; config.json then has Gamma as `vertical`. A played drag
    moves `top` by the drag; a flick carries on past the finger and stops; a touch during a flick stops it (no glide); a
    bottom tap glides on 960, a top tap back, two quick bottom taps (the second during the first glide) 1920 and two quick
    top taps back (D50), a middle tap opens the menu (after the double tap's 250 ms wait). Zoom (WP-13d,
    D50): a double tap at (200, 640) → `zoom` 2, `x` −200 (the strip's left edge), `top` + 320, `sharp` with `sharpPages` 0
    (Gamma's decode is full size: nothing decodes again); a zoomed sideways drag of 150 → `x` −50, `top` stays; a pinch
    around the middle from 200 to 300 apart → 3 × (`x` ≈ −255), from 300 to 200 → 2 × (`x` ≈ −50); a 400 drag at 2 × moves
    `top` 200; a double tap → 1 ×, `x` 0, `top` − 320. The Switch adds ZR / ZL (1.5, 2: `x` −360; a D-pad glide sideways
    to the strip's edge, `top` stays; back to 1). After the moves the page codes on screen are where the event's page and
    spot put them (within 3 px). At rest the strip sits on whole units (D50), so its goldens repeat. Goldens: the seam of
    pages 1 and 2 at the middle line (`webtoon-seam`,
    no gap), the band at the end of ch1 (`webtoon-band`); in page 2 the server has `lastPageRead` 1 and the spot note; ch1 is
    read on the server, then ch2 is the chapter read; back up shows ch1's last segment; closed half way down ch2's page 2,
    Continue puts the same segment at the same place. Alpha ch1 meanwhile opens paged in landscape (each reader keeps its
    orientation); Rotate in the strip → landscape, 640 wide, centred (`webtoon-landscape`); "Strip width: Full" → 1280,
    and Gamma opens like that next time; Alpha switched to "Vertical strip" by hand is a strip at the same page, back to
    "Left to right" shows it. `texUsed` ≤ 64 MiB, no `mem_pressure` or `error`. Scrolling to an exact place uses held drags
    (the finger rests before it lifts: no flick). ~70 s on desktop
7. `resume-progress` – Alpha ch3 to page 5, B: the server has lastPageRead 4, Continue opens page 5 (colour code), and again after a restart (WP-11)
8. `offline-errors` (WP-12) – the test server stops and starts again four times while the app runs, with `"diskCache": false`: at start-up
   (the Library tab shows the error + Retry, a tap on Retry connects once it is back); in the reader (prefetched pages still
   show, the next one shows its error, Retry by touch and X fail again, X works once the server is back, and the following page,
   which failed as a prefetch meanwhile, shows without another Retry); Refresh on the details screen (fails, the list stays); a
   manga opened while it is away (error + Retry); the library's Refresh (error + Retry). Every `error` event is of kind offline,
   network or timeout. The PC's firewall drops packets to the stopped server, so connections time out (5 s) rather than being
   refused: that is what a real server that is off looks like too, and it counts as "can't reach" (D45). ~2 min on desktop
9. `broken-images` – Broken ch1: pages 1–2 show an error with Retry (golden) and an `error` event, page 3 (1 × 1) and 4–5 show, turns never blocked, Retry by touch and by X fails again visibly (`retries`), B back (WP-11)
10. `soak` (WP-12; the console's numbers count, desktop runs it too) – the Big shelf flicked to the end and back, then 500 turns
    (`SNX_SOAK_TURNS`) through Delta Big's 2-page chapters (~167 chapters), then the shelf again: no `error` or `mem_pressure`
    events, every `mem` sample within the profile's cache budgets (+ 64 MiB) above the first, a clean quit (no crash report or
    fatal file). Writes `out/soak/summary.json` (memory first / after 100 turns / peak / last, turn times) for the profile
    tuning (D45). Desktop ~1.5 min
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
15. `touch-pass` (WP-12, D16) – every MVP screen by touch alone, no button until the quit (the Switch: handheld): the
    Settings tab and back, a category, Refresh, a cover; the description, Sort; the reader's edges, a swipe, the menu and its
    Close; back from the details and the setup screen by the footer's "Back" hint (borealis's hints take taps). ~10 s on desktop
16. `keyboard-wait` (WP-12 regression, Switch only) – the setup screen's address field opens the system keyboard, which
    stays open 15 s (it blocks the main loop, longer than the 10 s watchdog), + confirms, Continue reaches the library, no crash
    record in `logs/`. ~50 s
17. `updates-history` (WP-15, D52; WP-15b, D53; desktop and Switch; the Switch adds 20 tab rounds with memory flat) – a known state first (GraphQL: every chapter
    but Epsilon's unread at page 0, `removeNewChapter()`); Updates empty (golden `updates-empty`); `addNewChapter()`, Update
    library → `library_update` done (total 1, skipped 156, newChapters 1), the row "Today" + Epsilon Chapter 3 (golden
    `updates-new`); the row opens it (code 8/3/1), a page on, B → the row and History at page 2; Alpha ch1 → page 3 and ch2
    → page 2 from the Library → History reloads, one Alpha row with ch2 (golden `history`), a tap continues ch2 page 2; X in
    Updates read / unread on the server; the cover → details; Delta Big ch150 stamped through GraphQL, X refreshes History,
    a tap continues it (`chapter_open` ms, desktop ~22 ms). Events: `updates` / `history` (state, rows, groups, `first`
    rows with chapterId, mangaId, title, chapter, page, read, day; Updates also `newest`; hidden, pages, end),
    `library_update` (phase started / progress / done / stopped / nothing / poll_failed, total, finished, skipped, failed,
    newChapters, ms), `chapter_open` (from, chapterId, page, chapters, ms). RIGHT from the tab list onto the rows needs a
    second press after a touch (borealis's first one only shows the focus): `focusRows()`. **The queue (WP-15b):** Alpha ch3
    to page 4 (its 5 s save), `stopServer()`, page 5, B → `progress` ok false + `progress_queue` added; quit; `startServer()`
    (the server still has page 4), start with the same config → `server_check`, `progress_queue` sent, the server has page
    5, History shows it. Then ch3 to page 7 the same way, "another device" sets page 2 through GraphQL, the next start →
    `progress_queue` dropped (server moved), the server keeps page 2. Events: `progress_queue` (what added / sent / already /
    dropped / kept / taken, chapterId, page, read, reason, waiting), `history` gains `waiting` (rows with a "not saved yet"
    note) and `waitingChapterId` / `waitingPage` on such a first row. Desktop ~107 s, Switch ~280 s

## Golden screenshots
- Stored per driver: `tests/e2e/goldens/{desktop,switch}/<scenario>/<step>.png`.
- Compared with `pixelmatch` (per-pixel threshold 0.1, allowed diff 0.1 % of the pixels by default), with masks for clock/battery/animated
  regions. Masks are painted magenta into both pictures, and into the stored golden, so the PNG shows what is ignored.
- Update with `tools/e2e.sh <driver> <scenario> --update-goldens` only when a UI change is intended (and say so in the commit).
- Test builds draw the focus frame without borealis's moving glow (`testhooks::steadyScreenshots`, WP-09): the glow freezes
  wherever it was when the input stopped, so on the Switch two shots of the same screen differed by ~0.15 %. Release builds
  keep the glow.
- One drill's golden alone: `SNX_E2E_DRIVER=switch SNX_E2E_BUILD=~/build/suwayomi-nx/switch-test/suwayomi-nx.nro npx vitest run
  -c vitest.e2e.config.ts tests/e2e/scenarios/crash-drills.test.ts -t main-loop-exception` (add `SNX_UPDATE_GOLDENS=1` to update).
- On the Switch, sys-autopilot sometimes resets the connection of an input request, and the driver never repeats those (a
  press could happen twice). A loop that checks the app's state before every input may catch that and look again
  (`inputInLoop` in `library-browse`); a fixed sequence of presses must not.
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
