# IS-008 Leaving the reader shows its page pasted over the manga's details for a moment

Kind: bug fix · Status: done · Opened: 2026-11-24 · Closed: 2026-11-24
Outcome: fixed (34a95d4, D66); the owner's console try passed
Model: Opus · Owner: tried reading a chapter → B back on the console: "Looks clean"

## Report
The owner, 2026-11-24, on the Switch, own server: "When transitioning from the reader back to the comics details,
similar to IS-004, there's a jarring merging transition happening." The owner had a console screenshot of it open in the
Album. It shows the reader's page (a comic page) at full strength in the middle of the screen, and around it the manga's
details screen, also at full strength (cover, title, Continue button, description, chapter rows with the focus on one,
Refresh). The reader's own background (black / white / grey around the page) is already gone. The screenshot shows the
owner's library, so it stays out of the repo: `tests/e2e/out/is-008/` (git-ignored).

## What we checked
- 2026-11-24 (UTC+08:00): borealis's screen change (`FADE`, IS-004 / D64): going back, the details screen is drawn at
  full strength at once and the reader fades out over it in 200 ms. borealis fades a view by lowering its alpha
  (`View::getAlpha()`, which multiplies in the parents'), and its own drawings use it (`View::a()`: backgrounds, labels,
  `brls::Image`).
- The reader draws its page itself with NanoVG, and **with a fixed alpha of 1**:
  `ReaderView::drawPage` (`nvgImagePattern(..., 1.0f)`), and the long-strip reader the same
  (`StripCanvas::drawItem`), plus the loading spinner, the grey placeholders and the strip's text (fixed colors).
  Everything else on the reader screen is borealis's (the background from `applyLook`, the bars, the page number) and
  fades. So while the reader fades out, its background fades away and shows the details, but the page stays at full
  strength the whole 200 ms and then vanishes in one frame. That is the screenshot.
- Opening the reader does the reverse, less visibly: the background fades in, but a page that is already there
  (or the spinner) is drawn at full strength from the first frame, over the details.
- A search of `src/` for our own NanoVG fills (`nvgFillColor`, `nvgStrokeColor`, `nvgFillPaint`, `nvgImagePattern`):
  only `ReaderView.cpp` and `StripCanvas.cpp` have any. No other screen draws for itself.
- 08:02, the new check (below) on the code before the fix lists all 8 places: `ReaderView.cpp` the spinner and the page
  tiles; `StripCanvas.cpp` the spinner, the text, three placeholders and the tiles. On the fixed code: none.
- 08:00 – 08:05: another session ("Fix flaky server-downloads e2e", its own worktree, its issue is IS-007 on its branch,
  so this one is IS-008) was timing runs on the test server and uses the shared desktop test build. The PC recordings and
  the desktop e2e runs waited until it was done, so they wouldn't disturb its timings. The owner asked the two to
  coordinate; that session handed over the test server at 08:14 and goes next (one at a time, never both at once).
- 08:15 – 08:17, desktop test build, test server, Reading → Alpha → chapter 1 → B, recorded at 60 fps on the hidden X
  screen (a throwaway probe like IS-004's). Size of each frame-to-frame change (RMSE, 0 = same, 1 = all different):
  - **Before (the old reader code):** going back, a smooth fade (steps ≤ 0.04), then **0.43** in one frame: the page
    vanishing. Opening, **0.46** in one frame: the page popping in at full strength while the rest fades in. A frame
    in the middle of going back looks like the owner's screenshot: the page solid over the details screen.
  - **After the fix:** going back, the biggest step is 0.09 (the fade's first frame), then smaller each frame; opening,
    at most 0.096. No jump either way; mid-fade the page is blended with the details.
  - A 0.045 step after the fade, both before and after: the bottom button hints lighting up again (borealis greys them
    while it blocks input during the animation; IS-004 saw it too). Not ours to change.
- 08:29, the owner woke the console. HOME menu, nothing open (`console-before-upload.jpg`); the build (the 08:04
  `tools/build.sh switch` of 34a95d4's code, gate PASS) uploaded to `/switch/suwayomi-nx/suwayomi-nx.nro`, SHA-256
  matched (`5d1bfae3…`). The console was left on for the owner's try.
- 2026-11-24, the owner's try on the console (own server): "Looks clean".

## Cause
The reader's page, spinner, placeholders and strip text ignore the screen's fade (they are drawn with alpha 1 / fixed
colors), while the reader's background fades with it. So the page hangs over the details screen without its
background, then pops away.

## Planned fix (done, see What changed)
Small (two files in the reader's drawing, one new check). It changes only how strongly things are drawn, nothing in
reading, paging, threads, progress or the safety layer, so it stays an issue.

1. `ReaderView`: draw the page tiles with the view's alpha (`getAlpha()` in `nvgImagePattern`) and the spinner with
   `a(color)`. Pass the same alpha to `StripCanvas::draw`, which multiplies it into its tiles, placeholders, spinner and
   text. At rest the alpha is 1, so a still screen looks exactly as now (e2e goldens unchanged).
2. A check so new drawing code can't bring it back: `tests/contract/faded-drawing.test.ts` (`npm test`) fails, naming
   the file and line, on a NanoVG fill or stroke in `src/` whose color or paint doesn't go through the fade.
3. Prove it on the PC like IS-004: record leaving (and opening) the reader at 60 fps, before and after, and compare
   frame-to-frame changes. Before: a big one-frame jump at the end of leaving (the page vanishing). After: none.
4. Tests: unit tests, `npm test`, typecheck, the desktop reader scenarios in both modes; Switch build (gate), uploaded
   for the owner's try.
5. Docs: CHANGELOG (Fixed), D66 (the rule: our own drawing takes the screen's fade, next to D64).

## What changed
- `src/ui/reader/ReaderView.cpp`: the page tiles are drawn with the view's `getAlpha()`, the spinner with `a(color)`;
  the strip gets `getAlpha()` each frame.
- `src/ui/reader/StripCanvas.hpp/.cpp`: `draw(..., alpha)` keeps it for the frame; tiles, placeholders, spinner and
  text go through `faded()`.
- `tests/contract/faded-drawing.test.ts` (new, `npm test`): fails, naming file and line, on a NanoVG fill or stroke in
  `src/` that doesn't take the fade (checked: it lists all 8 places on the old code).
- CHANGELOG (Unreleased → Fixed), D66 (the rule, next to D64).
- Tests: unit tests, `npm test` (151 passed), typecheck green; desktop build 0 warnings. Desktop e2e, 08:17 – 08:26:
  smoke and the reader scenarios (reader-paged, -webtoon, -zoom, -portrait, -rtl-spread, -spread-split,
  offline-reading, resume-progress) in both modes: **18 / 18 green**, goldens unchanged. Switch: `tools/build.sh
  switch` (gate PASS), uploaded to the console (SHA-256 matched). No Switch e2e run: the change only scales the alpha of what's drawn, and the desktop runs cover
  every reader path that draws.

## For the owner
Nothing. Tried on the console 2026-11-24 (open a chapter, B back): "Looks clean".

## If it comes back
Capture a frame within 200 ms of B in the reader (desktop build, or `sa.sh -o shot.jpg GET /screenshot` right after B on
the Switch). A page or spinner at full strength over the details points to drawing that skips the fade: the check should
have caught it, unless it's a new way of drawing (not `nvgFillColor` / `nvgStrokeColor` / `nvgFillPaint` /
`nvgStrokePaint` / `nvgImagePattern`). On the PC: `tests/e2e/out/is-008/is008-fade-probe.test.ts.txt` (copy it into
`tests/e2e/scenarios/` as `.test.ts`, run it with `SNX_PROBE_LABEL=<name> tools/e2e.sh desktop is008-fade-probe`) records
it at 60 fps; `steps.sh <frames dir> 13 30` next to it prints each change (a single step above ~0.3 = the pop is back).
The owner's screenshot, the recordings (`before/`, `after/`, the two side-by-side PNGs) and the e2e logs (`e2e/`) are in
`tests/e2e/out/is-008/` (git-ignored; may be gone later).
