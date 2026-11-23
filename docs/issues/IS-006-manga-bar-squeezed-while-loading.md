# IS-006 While a manga's chapters load, Sort / Show / Refresh are squeezed

Kind: bug fix · Status: open · Opened: 2026-11-23 · Closed: –
Outcome: –
Model: Opus · Owner: – (paused at the usage limit 2026-11-23 22:05 (UTC+08:00) at the owner's word; resumes after the reset)

## Report
The owner, 2026-11-23, on the Switch, normal build, own server: "When selecting a manga in the library, there's this weird
moment while the details and chapters of the manga are loading where sorters(?) in the upper right are not displaying
well." Screenshot (the owner's library, so it stays out of the repo): `tests/e2e/out/is-006/owner-screenshot.jpg`
(git-ignored), taken with `sa.sh -o ... GET /screenshot` while the console showed it in the Album.

In the screenshot ("Loading chapters..." in the middle): the three bar buttons are crammed into the last ~270 px on the
right. "Sort: Newest first" runs out of both sides of its box, "Show: ..." and "Refre..." are cut with an ellipsis.
Once the chapters are there, the bar looks right (e2e: Sort at x 942, Show at 1100, Refresh at 1211).

## What we checked
- 21:56 – 22:05 (UTC+08:00), reading code only (nothing built or run yet):
- `resources/xml/activity/manga.xml`, `manga/bar`: a row with the chapter count label (`width="auto"`, `grow="1.0"`) and
  the three buttons (no width: as wide as their text). `MangaActivity::updateBar()` gives the count label **no text**
  ("") while the state is Loading or Failed; it gets "N chapters" once the list is there.
- borealis's label measuring (`library/borealis/library/lib/views/label.cpp`, `labelMeasureFunc`, lines 85–99): when the
  text is **empty** and neither the width nor the height is fixed, it returns the **whole width it was offered** instead of 0.
  So while loading, the empty count label asks for the bar's whole width (~796 px) as its starting size, and Yoga
  (web defaults: everything may shrink) shrinks all four items to fit, the buttons below their text's width. That matches
  the screenshot (the label ends ~513 px wide, the buttons ~118 / 72 / 59 px).
- The other "fill the rest of the row" labels (`categories/note`, `picker/note`, `filters/note`, `search/progress`,
  `languages/note`, `browse/note`, the library's and a source's search labels) always have text when they're shown
  (set on every update, or their box is hidden while empty), so they don't show this. The code-built status lines in
  Downloads / Updates / History sit in a growing column and always get text too.

## Cause
borealis measures an empty label as "as wide as possible", and the chapter count is empty while the chapters load (and
after a failed load), so it pushes the bar's buttons together.

## Planned fix (not started)
1. `manga.xml`: the count label gets `width="0"` (keeps `grow="1.0"`): it starts at nothing and takes only the space the
   buttons leave. When everything fits the bar looks exactly as now (same goldens). A count line too long for the space
   (e.g. offline: "Offline: the chapters on this Switch · ...") then wraps to a 2nd line (or ends in "...") instead of
   squeezing the buttons. Leave `singleLine` off so it can wrap.
2. Probably the same `width="0"` on the other row-filling labels above (they're fine today, but a long text would squeeze
   their buttons the same way), plus a small check in `tests/contract/` (like `screen-backgrounds.test.ts`) that a Label
   with `grow` inside a row has `width="0"`. Decide while doing it; a D-entry only if it becomes a rule.
3. Prove it on the PC: a throwaway probe scenario (kept in `tests/e2e/out/is-006/`) that sets the test source's
   `delayMs` (`setTestSourceControl`, `tests/e2e/lib/test-source.ts`), opens a test-source manga (Browse → SNX Test (EN)
   → a cover, as `browse.test.ts` step 9 does) and screenshots the bar while it loads, before and after the fix.
4. Tests: `tools/build.sh tests`, `npm test`, desktop e2e `manga-details` and `browse` in both modes (they tap the bar),
   then a Switch build for the owner to try (console asleep: the owner wakes it).

## What changed
Nothing yet.

## For the owner
Nothing yet.

## If it comes back
Open a manga whose chapters load slowly and look at the bar; a squeezed bar = a growing label with no text and
`width="auto"` somewhere in that row.
