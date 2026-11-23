# IS-006 While a manga's chapters load, Sort / Show / Refresh are squeezed

Kind: bug fix · Status: waiting on owner · Opened: 2026-11-23 · Closed: –
Outcome: fixed on the PC (D65); the owner's try on the console is still to come
Model: Opus · Owner: asked (2026-11-24) to fix every screen with the same kind of bug; to come: open a manga from the
library on the console (the new build is on it)

## Report
The owner, 2026-11-23, on the Switch, normal build, own server: "When selecting a manga in the library, there's this weird
moment while the details and chapters of the manga are loading where sorters(?) in the upper right are not displaying
well." Screenshot (the owner's library, so it stays out of the repo): `tests/e2e/out/is-006/owner-screenshot.jpg`
(git-ignored), taken with `sa.sh -o ... GET /screenshot` while the console showed it in the Album.
2026-11-24, while the fix was being made: "If there's any instance of this type of bug on other screens, make sure to fix
them as well on the fix".

In the screenshot ("Loading chapters..." in the middle): the three bar buttons are crammed into the last ~270 px on the
right. "Sort: Newest first" runs out of both sides of its box, "Show: ..." and "Refre..." are cut with an ellipsis.
Once the chapters are there, the bar looks right (e2e: Sort at x 942, Show at 1100, Refresh at 1211).

## What we checked
- 2026-11-23 21:56 – 22:05 (UTC+08:00), reading code (paused at the usage limit at 22:05, resumed 2026-11-24 ~06:15):
  - `resources/xml/activity/manga.xml`, `manga/bar`: a row with the chapter count label (`width="auto"`, `grow="1.0"`) and
    the three buttons (no width: as wide as their text). `MangaActivity::updateBar()` gives the count label **no text**
    ("") while the state is Loading or Failed; it gets "N chapters" once the list is there.
  - borealis's label measuring (`library/borealis/library/lib/views/label.cpp`, `labelMeasureFunc`, lines 85–99): when
    the text is **empty** and neither the width nor the height is fixed, it returns the **whole width it was offered**
    instead of 0. So while loading, the empty count label asks for the bar's whole width (~796 px) as its starting size,
    and Yoga (web defaults: everything may shrink) shrinks all four items to fit, the buttons below their text's width.
    That matches the screenshot (the label ends ~513 px wide, the buttons ~118 / 72 / 59 px).
- 2026-11-24 06:20 – 06:23, desktop, a throwaway probe scenario (`tests/e2e/out/is-006/is006-bar-probe.test.ts.txt`,
  run by `probe.sh <label>` there: test server reset first, so the manga isn't on the server yet): Browse → SNX Test (EN)
  → Midnight Knight with the test source answering after 4 s; a screenshot 0.7 s after the screen opens ("Loading from
  SNX Test (EN)...") and one once the chapters are there. Both modes.
  - **Before** (`before/*-loading.png`): the same squeeze, at the same pixels as the owner's console screenshot.
  - **After** `width="0"` on the count (`after/*`): while loading the bar is **pixel-identical** to the loaded bar's
    buttons (ImageMagick `compare -metric AE` on the bar: 0); the loaded screen is pixel-identical before vs after (0
    pixels above the clock row). Same in applet mode.
- 06:25 – 06:29, the offline case (a temporary screenshot in `offline-reading`, not committed): the count line "Offline:
  the chapters on this Switch · 2 chapters" is longer than the room left. **Before**: it wrapped and also squeezed the
  buttons a little (their inner padding gone). **After**: it wraps to two lines, the buttons keep their size
  (`before/` and `after/offline-application.png`).
- 06:26 – 06:31, the same kind of bug elsewhere (the owner's request). A new check (`tests/contract/row-fill-width.test.ts`)
  lists every view in `resources/xml/` that grows in a row and starts at its content's width:
  - **Same pattern, a view showing text** (a Label or a box holding one), 12 in all: the manga count and the manga
    screen's right column; the notes / progress lines on Categories, the category picker, Filters, Global search,
    Languages and the Browse tab; the search line (and its box) on the Library tab and a source's screen; the Settings
    list. Today only the manga count is ever empty while shown (the others get text on every update, or their box is
    hidden while empty); a long text (a long search, a long translation) would squeeze their buttons the same way.
  - **Not the same:** growing Buttons with fixed words that share a row on purpose (setup's Test / Continue / Cancel,
    login's two) and the box of a source's Popular / Latest buttons. `width="0"` there would change their sizes (equal
    widths), so they stay; the check skips Buttons and boxes of Buttons.
  - **Bars built in code** (Server / Switch downloads, Updates / History headers): their buttons have a fixed width, which
    borealis also makes the least width (`View::setWidth` sets the min width), so a long or empty status line can only
    shrink its own column. Not affected.

- 06:31 – 06:35, tests: unit tests (14 s, contract 57/57), `npm test` (15 files, 149 tests) and typecheck green. Desktop
  e2e (stops at the first failure): `browse` red in both modes on golden `source-popular` (3 % of pixels). The layout
  was identical; the **covers** were wrong (Comet Courier showed EN-40's cover, Lost Cover Mystery had one). Cause, in
  the test harness, not the app: Suwayomi keeps the covers of manga outside the library in
  `<java temp>\Tachidesk\thumbnails\<manga id>.jpg`, i.e. Windows' `%TEMP%`, which a test-server reset didn't clear. After
  a reset the same ids go to other manga (the order the source's manga first reach the server), so after this session's
  probe runs (several resets, each browsing in a different order) the server served old covers under new ids. Fix:
  `tools/test-server/test-server.ps1` starts Java with `-Djava.io.tmpdir=<root>\tmp`, so the cache lives in `root\` and
  every reset wipes it (and it stays apart from any other Suwayomi on the PC). `%TEMP%\Tachidesk\` is left as it is.
- 06:36 – 07:15, the whole desktop suite again, both modes, a fresh reset before each, `--bail 0` (`run-e2e.sh`):
  **applet mode all green** (29 files, 1 skipped as always). Application mode: 26 green, 3 red: `browse`
  (`source-popular` again; its pictures were overwritten by the applet run), `server-downloads` (no idle within 15 s) and
  `switch-downloads` (a downloaded strip page reported from somewhere other than the SD card or memory).
  The server kept its covers in `root\tmp\Tachidesk\thumbnails\` (60 files): the tmpdir change works.
- 07:16, the three alone, application mode, fresh server (`rerun.sh`): **all green**.
- 07:21 – 07:35, the whole application suite once more (`rerun-full-app/`): **28 green**, `browse` and
  `switch-downloads` green; only `server-downloads` red, at the same step as before (Server downloads tab → a row → Move
  to top, `server-downloads.test.ts:311`): after the move the app's next queue request got no answer for 15 s. The
  server's log (`root\logs\application.log`): its downloader had started a chapter ("Running: 1, Queued: 6") while every
  source call was slowed by 5 s on purpose, and the queue request waited behind it. IS-004 saw the same failure before
  any of this ("the app stayed busy > 15 s on a request while the test source was slowed on purpose"). Neither change
  touches the download queue (that tab is built in code and unchanged), and it passed in applet mode and alone: a
  flaky scenario that this issue didn't cause. Left as it is here (a follow-up was offered).
- 07:36, Switch build (`tools/build.sh switch`): 0 warnings, safety gate PASS. The console didn't answer
  (`sa.sh GET /status`: asleep), so the build isn't on it yet.
- 07:48, the owner woke the console. HOME menu, nothing open (screenshot `console-before-upload.jpg`); the build (the
  07:42 `tools/build.sh switch` of 37f6211's tree, gate PASS) uploaded to `/switch/suwayomi-nx/suwayomi-nx.nro`
  (`deploy` in `tools/spike/lib.sh`), SHA-256 matched. The console was left on for the owner's try.

## Cause
borealis measures an empty label as "as wide as possible", and the chapter count is empty while the chapters load (and
after a failed load). It started at its content's width (`width="auto"`), so it claimed the whole bar and the bar's
text-sized buttons were squeezed together. A long count line (offline) did the same, less.

## What changed
- `resources/xml/`: `width="0"` (was `"auto"`, `grow` kept) on the 12 views above: `activity/manga.xml` (count, right
  column), `categories.xml`, `category_picker.xml`, `filters.xml`, `global_search.xml`, `languages.xml`, `source.xml`
  (search box and line), `tabs/browse.xml`, `tabs/library.xml` (search box and line), `tabs/settings.xml` (the list).
- `tests/contract/row-fill-width.test.ts` (new, `npm test`): fails, naming file, view and line, on a view that grows in a
  row, shows text and has no number width. It found the 12 before the change.
- D65 (the rule), CHANGELOG (Unreleased → Fixed).
- Test harness: `tools/test-server/test-server.ps1` starts Java with its temp folder in `root\tmp` (reset wipes the
  server's cover cache); `tools/test-server/README.md` says so.
- Tests: unit tests, `npm test` (with the new check), typecheck green. Desktop e2e: applet mode all green; application
  mode all green but `server-downloads` (an existing flaky step, What we checked). Switch build: gate PASS, on the
  console (SHA-256 matched).

## For the owner
The new build is on the console: open a manga from the library whose chapters take a moment. Sort / Show / Refresh
should keep their size while "Loading chapters..." shows. The `server-downloads` flake is being looked at in its own
session.

## If it comes back
Open a manga whose chapters load slowly and look at the bar. On the PC: `tests/e2e/out/is-006/probe.sh <label>` (copy
`is006-bar-probe.test.ts.txt` into `tests/e2e/scenarios/` as `.test.ts` first; build with `tools/e2e.sh desktop
is006-bar-probe`) screenshots the bar while loading. A squeezed bar = a view in that row growing from `width="auto"`
(the check should catch it in a layout; a bar built in code isn't checked). The owner's screenshot, the before / after
shots and the test logs are in `tests/e2e/out/is-006/` (git-ignored; may be gone later).
