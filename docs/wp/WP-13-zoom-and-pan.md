# WP-13 Zoom & pan (paged reader)

Model: **Opus** · Size: L (split point below: spread splitting can move to WP-13c) · Needs: WP-22 · Owner: try it in your
hands (pinch, double-tap, ZL/ZR, the sticks, both orientations) on a real chapter from your server; console

Planned 2026-10-30 (planning session, owner's answers below). Runs **next**, before WP-13b (webtoon), which reuses its zoom
math, its two-finger handling and its test-hook touch channel.

## Goal
Today the reader shows a page only at its fit: in landscape a regular page (1000 × 1500) is drawn about 480 × 720 and its
text is small; in portrait about 720 × 1080. After this WP you can zoom into any part of a page (pinch with two fingers,
double-tap the middle, ZR / ZL) up to 4 ×, move around in it (drag, either stick, the D-pad), and the zoomed page is
**sharp**: the reader decodes the page again at the size it is drawn, as far as the original image has detail, instead of
stretching the small screen-sized copy. Held sideways, a double-page spread is split into two halves that each fill the
screen, instead of one tiny 720 × 540 picture. Both orientations, and docked (buttons only). It also fixes WP-22's leftover:
holding the D-pad in portrait turns several pages (D46). The owner's reason for doing zoom first: bigger, sharper text on
regular manga pages.

## The owner's answers (2026-10-30)
- **Turning the page while zoomed: back to the whole page** (like Mihon). A reader setting "Zoom on page turn" offers
  **Back to whole page** (default) / **Keep the zoom** (the next page opens at the same zoom, at the corner where reading
  starts: top right for right to left, top left otherwise).
- **Double-tap: the middle of the page only.** A double tap in the middle zone zooms to 2 × at the tapped point; another
  double tap goes back to the whole page. Taps on the left and right edges still turn pages at once. Only the single middle
  tap (it opens the menu) waits about a quarter second, to see whether a second tap follows.
- **Spreads: split in portrait only** (default). In landscape a spread stays whole (a half would be no bigger there: 480 ×
  720 either way). A reader setting "Split spreads" offers **In portrait** (default) / **Always** / **Never**. The order is
  mirrored for right to left: the right half first.
- **Sticks when not zoomed: like now.** Left/right turns the page, up/down scrolls a page taller than the screen; either
  stick, so in portrait the stick on the bottom Joy-Con works. Zoomed in, both sticks pan smoothly.

## Planner's defaults (the owner can change them after trying; record them all in a new decision D48 at the end)
- **Zoom range:** from the fit (1 ×) to `RuntimeProfile::maxZoom` (Full: 4; safe mode and the applet profile: 2). ZR zooms
  one step in, ZL one step out (1 → 1.5 → 2 → 3 → 4), around the middle of the screen, repeating while held. The zoom is
  not saved: each opening of the reader starts at the fit. Rotating (Y) and changing fit go back to the fit.
- **Pinch:** the point between the two fingers stays under them, and the page follows them as they move together. Let go
  below 1.05 × and it goes back to the fit. While two fingers are down, no tap, drag or swipe counts; when one finger lifts
  the pinch ends, and the other finger does nothing until it lifts too.
- **Drag** pans a zoomed page like any page bigger than the screen (D44), with no flick momentum (WP-13b builds momentum for
  the strip; the zoomed page can take it then). A sideways swipe turns only when the page was already at its edge (D44).
- **D-pad, zoomed:** moves the view about three quarters of the screen that way, gliding for about 0.15 s. At the page's
  edge, a fresh press toward a side turns the page (like a swipe at the edge). Up/down at the top or bottom edge do nothing.
- **Sticks, zoomed:** pan smoothly, faster the further pushed (a dead zone of about 15 %; a full push crosses the screen in
  about 0.6 s). A push that starts while the page is already at its edge that way turns the page, like the D-pad.
- **One press, one page (the D46 fix, both orientations):** holding a button or a stick never turns more than one page; a
  turn needs a fresh press. Scrolling and panning repeat while held.
- **Unchanged:** L/R always turn (also zoomed), A the menu, B closes (it does not zoom out first), X retry, Y rotate,
  MINUS settings. While the menu is open any tap closes it (D44), so there is no double-tap then. The page number shows the
  page; both halves of a split spread are the same page (the server has one page there). Fit "Original" stays hidden: zoom
  replaces it.

## Read first
`docs/STATUS.md`; D16, D39, D44, D46 in `docs/DECISIONS.md`; `src/core/README.md`, `src/core/ReaderMath.hpp`,
`ImageGeometry.hpp`, `AppConfig.hpp`, `RuntimeProfile.hpp`; `src/image/README.md`, `ImageLoader.hpp` (and `clampBox` in
`ImageLoader.cpp`), `Caches.hpp`, `DecodedImage.hpp`; `src/ui/README.md` and all of `src/ui/reader/` (this WP changes them,
so read the `.cpp` files too); `src/app/TestHooks.hpp` and `.cpp` (the touch channel goes there); `tests/e2e/README.md`,
`tests/e2e/lib/reader.ts`, `lib/screens.ts`. From borealis only: `ControllerState`, `TouchState` and the button enum in
`library/include/borealis/core/input.hpp`; the touch loop in `lib/core/application.cpp` (about lines 260-350: every finger
gets its own hit test and goes to that view's recognizers with its `fingerId`); `View::gestureRecognizerRequest` in
`lib/core/view.cpp`; `lib/core/touch/pan_gesture.cpp` (it follows only the first finger, `lastFingerId`) and
`tap_gesture.cpp`; the button tables in `lib/platforms/switch/switch_input.cpp` and `glfw/glfw_input.cpp`.

## How (the planner's suggestion; the session may find a better way)
1. **Math in core (pure, unit-tested), in `ReaderMath`.** Zoom 1 = the fit; the drawn size is `fitPage(...)` × zoom.
   `zoomAround` (the new top-left corner so that the point under the anchor stays put, then `clampPosition`), the pinch
   (zoom from the finger distance against the distance at the start, the anchor and the pan from the fingers' midpoint),
   `zoomStep` (the steps above, capped at `maxZoom`), the snap back to the fit, the double tap (second touch-down within
   250 ms of the first lift and within 40 units, middle zone only; both taps may slide like D46's `isTap`), what an arrow
   or a stick push does (pan by a step, turn at the edge, nothing; the fresh-press rule), the stick's speed (dead zone,
   curve) and `readerStick` (the stick turned with the screen, like `readerArrow`). Spreads: `splitsSpread(mode,
   orientation, isSpread)`, the halves' order by direction (right to left: right first; left to right and vertical: left
   first) and their rectangles in the decoded image (odd widths). New enums with names in `AppConfig`: `ZoomOnTurn`
   (`reset` / `keep`) and `SpreadSplit` (`portrait` / `always` / `never`), saved as `reader.zoomOnTurn` and
   `reader.splitSpreads`, for all manga like the background (new keys, old files lack them: no migration).
2. **Sharper pages: decode the whole page again at a bigger box.** The box the page on screen wants = the fit's box
   (`decodeBoxFor`, real pixels of the reader's screen) × the zoom rounded up to the next step (a pinch to 1.7 decodes for
   2, so small changes don't decode again), and twice as wide for a split spread (each half must be sharp). It is capped
   per side at `maxDecodeScale` × the reader's screen as it is held (portrait: 2 × 720 wide, 2 × 1280 tall), and as always
   by the source size (never bigger than the original, D39) and `maxDecodeBytes`. Today the loader caps every box against
   the landscape screen (2560 × 1440), which would leave a high-resolution page in portrait (fit Screen) blurry above
   1.33 ×: make the loader's cap
   orientation-free (each limited side at most `maxDecodeScale` × the screen's longer side) and let the reader's core
   function do the exact cap. The new decode starts once the zoom has settled (no change for 150 ms, or the fingers
   lifted), at `Reader` priority; the fit image stays on screen, scaled by the GPU, until the sharper one arrives, then
   `ReaderView` swaps the textures (reserve the new ones first, free the old after; S7) and keeps zoom and position: it
   must know the new image is the same page (today a new image restarts at the start position, and `pageRevision` counts
   it as a new page). Zooming back to the fit swaps back to the fit decode (still in the memory cache, or decoded again).
   Prefetched pages stay at the fit's box; with "Keep the zoom" a turn shows the next page's fit decode at the zoom at once
   and makes it sharp the same way. Turning away cancels a zoom decode under way. **`maxDecodeScale` decision:** Full keeps
   2 unless the console numbers below allow 4 (then a high-resolution page stays sharp up to 4 ×; most pages of about
   1000 × 1500 run out of detail near 2 × in landscape and 1.4 × in portrait anyway); write the measurement and the
   choice in D48. Rejected: decoding only the part on screen (libjpeg-turbo and libwebp can crop, libpng can't; it needs a
   cache per region and new decodes while panning; a whole 1000 × 1500 page is only 6 MB).
3. **Touch.** Our own pinch recognizer (a `brls::GestureRecognizer` subclass added with `addGestureRecognizer`) keeps every
   finger by `fingerId`; while it sees two, the reader ignores the pan recognizer's moves and taps. In portrait the points
   go through `core::screenToReader` first (WP-22). Double tap in `ReaderView`: a middle tap waits 250 ms (drawn from the
   clock, like the spinner) before it opens the menu; a second tap in time zooms instead. Edge taps don't wait.
4. **Controller.** ZL/ZR are `BUTTON_LT` / `BUTTON_RT` (repeat on). For the sticks, borealis's NAV buttons are "D-pad or
   either stick" on the Switch (`HidNpadButton_AnyUp`...); the D-pad alone is `BUTTON_UP`/`DOWN`/`LEFT`/`RIGHT` (desktop
   arrow keys too), and the axes are in `brls::Application::getControllerState()`. Suggested: the D-pad from `BUTTON_*`, the
   sticks read from the axes once a frame (not zoomed: a push past half way counts as one D-pad press; up/down scroll a
   tall page smoothly; zoomed: smooth pan). The fresh-press rule: remember the button or stick that turned, and ignore it
   until it is let go.
5. **Spreads.** `ReaderModel` keeps a half (first / second in reading order) for the page on screen. A step from the first
   half of a split spread goes to its second half; a step back onto a spread lands on its last half. A page is known to be
   a spread only once decoded, so a prefetched spread that is to be split loads again at the doubled box at once (usually
   before it is on screen); stepping back onto a page not decoded yet remembers "land on the last half". Progress and the
   prefetch window count pages, not halves (`lastPageRead` is the page). Rotating to landscape (with "In portrait") shows
   the whole spread; back to portrait starts at its first half. `ReaderView` draws a half as a part of the page's tiles
   (scissor + offset); fit, zoom and pan treat the half as the page.
6. **Test hooks (test builds only).** (a) **A touch channel** (planned in TESTING.md and D16 since WP-03, built now): the
   driver writes `<data>/test/commands/<n>.json`; `testhooks::onFrame` looks about every 100 ms, reads it with
   `SafeFs::readFile` (at most 64 KiB, S6), skips a file that doesn't parse yet (the Switch's upload writes in place), runs
   it, deletes it (`SafeFs::removeFile`) and writes a `command` event (`id`, `ok`). One command now: `touches`, frames of
   finger positions (`ms`, `fingers: [{id, x, y}]`, landscape screen units like the drivers' touches), played one frame per
   app frame into the reader view through borealis's own `gestureRecognizerRequest` (the same recognizers a real finger
   reaches; only borealis's hit test is skipped; if a test-build input wrapper can inject them before the hit test
   instead, prefer that). Helpers `pinch(cx, cy, fromDistance, toDistance, ms)` and `doubleTap(x, y)` in `tests/e2e/lib/`,
   for both drivers (the Switch writes the file over sys-autopilot). sys-autopilot-nx has one finger and needs ≥ 150 ms
   between requests, so it can't pinch or double-tap by itself; the owner's hands check the real touch path. (b) **Events:**
   `reader` gains `zoom`, `decW`/`decH` (the decoded image drawn), `sharp` (it has the detail the zoom wants, as far as the
   source and the cap allow), `sharpMs` (from the last zoom change to the frame that draws the sharper decode), `split`,
   `half`, `texUsed` (`TextureBudget::used`); a new `gesture` event after every pinch, drag, stick pan, D-pad glide and
   zoom step: `kind`, `frames`, `p90Ms`, `maxMs` (frame intervals while it ran), `zoom`, `x`, `y`.
7. **Settings menu:** "Zoom on page turn" and "Split spreads" join direction, fit, background, page number and orientation.

## Deliverables
- [ ] **Spike first** (short): two real fingers reach `ReaderView` with their ids on the console (the session can't touch
      the console with two fingers: read borealis's touch loop to be sure, log the finger ids, and ask the owner for a
      ten-second pinch with the log on), and a played `touches` frame reaches the same recognizers on desktop. If borealis
      fights this, stop at a clean point and write what you found in STATUS before building the rest.
- [ ] core: the zoom, pinch, double-tap, arrow/stick, fresh-press and spread math above; `ZoomOnTurn`, `SpreadSplit` +
      names and config keys; `RuntimeProfile::maxZoom` (Full 4, safe mode and applet 2); the zoomed decode box with the
      reader-screen cap. Unit tests: zoom steps and limits, `zoomAround` keeps the anchor point fixed and never shows a gap,
      the pinch (scale, midpoint pan, snap to fit), buckets (1.2 → 1.5, 1.7 → 2), boxes per fit and orientation (portrait
      2 × is sharp), split ×2, the source-limited "sharp" rule, double tap in/out of time and distance and only in the
      middle, arrows and sticks zoomed / not zoomed in all three orientations, the fresh-press rule, the dead zone and
      speed, `splitsSpread` for every mode × orientation, the halves' order and rectangles, config round trip and old files
- [ ] image: the loader's orientation-free cap (unit test: a portrait box at 2 × isn't cut to 1440 tall)
- [ ] `ReaderModel`: the wanted box for the page on screen (zoom), the fit image kept until the sharper one arrives,
      cancel on turn, spreads found after decode loaded again at the doubled box, halves in `step()`, progress by page,
      both zoom-on-turn modes. Unit tests (suite `ui`): through a spread both ways, backward lands on the last half, a
      spread found while prefetched, the zoom box only for the page on screen, cancel on turn
- [ ] `ReaderView`: zoom and pan drawing, halves, the texture swap that keeps zoom and position, the pinch recognizer,
      the double tap and the menu's wait, glides from the clock, stick pan each frame, the `touches` entry for tests
- [ ] `ReaderActivity`: ZL/ZR, the D-pad and sticks by the rules above, the fresh-press fix, the two new settings, the
      new event fields and the `gesture` event
- [ ] Test hooks: the touch channel (`command` event), `pinch()` / `doubleTap()` helpers, TESTING.md "Drivers" says how
- [ ] Scenario `reader-zoom` (desktop and Switch). Desktop: Alpha ch1 in landscape: a double tap in the middle → zoom 2,
      the tapped point stays put (`x`/`y`), then the sharper decode (`decW` 1000, `sharp`; golden); a drag to the right
      edge and further (`x` stops at the edge); a swipe at the edge turns → page 2 at zoom 1; a played pinch in (zoom 3
      ± 0.1) and out to the fit; a double tap back to the fit; a single middle tap opens the menu (after the wait); an edge
      tap turns without the wait (`turnMs` < 100 on desktop); "Zoom on page turn: Keep" → a turn keeps zoom 2 at the
      reading start corner (the setting survives a restart); the same double tap, pinch and drag in portrait turned right (points
      from `portraitPoint`; golden). Switch adds: ZR twice → 2, ZL back; a stick pan (`/input/stick`) and a fresh push at
      the edge turns; D-pad steps and the edge press; not zoomed, a stick held 2 s turns one page; in portrait, holding the
      D-pad that turns pages turns exactly one; the frame numbers and `sharpMs` below; 50 zoom cycles; 100 turns from
      zoomed pages
- [ ] Scenario `reader-spread-split` (desktop and Switch): Beta RTL ch1 in portrait ("In portrait"): page 4 shows its
      right half first (`split`, `half` 0, sharp: the spread decoded 1440 wide, so the half drawn 720 wide has 720
      pixels, not the 360 of the fit decode), the next step its
      left half, then page 5; back lands on page 4's left half; the server's `lastPageRead` is page 4 (index 3) on both
      halves; landscape shows it whole (960 × 720, as in `reader-rtl-spread`); "Always" splits it in landscape too; left
      to right shows the left half first; "Never" shows it whole in portrait (720 × 540). Goldens of both halves
- [ ] The existing scenarios stay green and unchanged: `reader-paged`, `reader-rtl-spread`, `reader-portrait`,
      `touch-pass`, `broken-images`, `soak` (the middle tap's wait only makes the menu open a quarter second later)
- [ ] Console (application mode, handheld, stock clocks, D17): the numbers in Acceptance; Switch build's safety gate PASS
- [ ] Owner tries it in their hands: pinch and double-tap (the real touch path), ZL/ZR, the sticks, both orientations,
      a spread in portrait, on a real chapter from their server; and says whether "Back to whole page" feels right
- [ ] Docs: D48 (the answers, the defaults as they ended up, the `maxDecodeScale` measurement and choice, the owner's
      verdict), ARCHITECTURE.md (reader math line, the "Max decode resolution" row), TESTING.md (scenarios 6c and 6d, the
      touch channel, the new events), `src/core/README.md`, `src/image/README.md` (the cap), `src/ui/README.md`,
      `tests/e2e/README.md` (the helpers), the user part of README.md (how to zoom), CHANGELOG.md (a version is the owner's call)

## Acceptance
`tools/build.sh tests` green with the new unit tests. `tools/e2e.sh desktop` green for every scenario.
`tools/e2e.sh switch reader-zoom`, `reader-spread-split`, `reader-paged` and `reader-portrait` green in handheld mode.
On the console (application mode, handheld, stock clocks):
- **Smooth:** while a played pinch, a stick pan or a D-pad glide runs, frame intervals p90 ≤ 17 ms (60 frames a second)
  and none over 34 ms, also the frame that uploads a sharper page (if an upload doesn't fit, upload one tile per frame).
- **Sharp soon:** an Alpha page (JPEG 1000 × 1500, from the memory or SD-card cache) is sharp within 0.35 s of the zoom
  stopping (`sharpMs` p90, the 150 ms wait included). Beta's spread split in portrait at zoom 4 (PNG 2000 × 1500: at
  `maxDecodeScale` 2 it decodes 1440 wide, at 4 the full 2000): `sharpMs`, the upload frame and memory measured at both
  and written down, ≤ 0.5 s at the one chosen; this decides `maxDecodeScale` (How, step 2). Alpha alone can't decide it:
  its pages run out of detail before either cap (the 15 % shrink slack keeps 1000 × 1500 whole).
- **Memory flat:** 50 zoom cycles (ZR up to 4, ZL back to 1) grow memory by < 16 MiB after the fifth, and `texUsed` is
  back to the fit page's after each zoom out. 100 turns from zoomed pages: prefetched turns p90 < 150 ms (as WP-11),
  growth < 16 MiB after turn 20. No `mem_pressure`, no `error` event, no crash report.
The owner has read a real chapter zoomed in, in both orientations, and is OK with how it feels.

## Split point
If the session runs long: zoom, pan, sharper pages, the controller rules and the D-pad fix land first, green, with their
tests, `reader-zoom` and the console numbers. Spread splitting (its math, the model's halves, the setting and
`reader-spread-split`) then moves to a new **WP-13c** in ROADMAP.md (Needs 13, before 13b), and STATUS says so.

## Sonnet-safe chunks
- The docs updates at the end (from a list of facts Opus writes).
- The `pinch()` / `doubleTap()` frame builders in `tests/e2e/lib/` (pure TypeScript from a spec of the frames and a vitest
  test), and refreshing goldens after an intended look change (the harness diffs, Sonnet re-runs).
- Not the zoom math, the decode boxes, `ReaderModel`, `ReaderView`, the touch channel or the controller rules (the reader
  and the image pipeline are never Sonnet, ROADMAP).

## Not in this WP
Flick momentum for drags (WP-13b); prefetching pages at the zoomed box for "Keep the zoom" (only if the owner finds the
short blur after a turn bothersome: it costs zoom² the memory per prefetched page); turning a spread back to landscape by
itself; guided panel-by-panel reading; a two-finger or double-tap route in sys-autopilot-nx (a sysmodule change, its own
small WP if the owner wants the console to pinch by itself); zoom in the webtoon strip (WP-13b reuses this WP's math).
