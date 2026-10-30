# WP-22 Portrait reader (hold the Switch sideways)

Model: **Opus** · Size: M · Needs: WP-12 · Owner: try it in your hands (both ways), say which way you hold it and whether the buttons feel right

Runs **right after WP-12, before WP-13** (owner, 2026-10-30: reading in portrait is one of the main reasons for the app).
It got the next free number so the many existing mentions of WP-13 … WP-21 stay right.

## Goal
The reader can be read with the console held sideways (portrait), the way a phone or an e-reader is held. Today a manga page
fitted to the screen's height is only about 500 × 720 pixels in the middle of a wide screen. Turned to portrait and fitted to
the width it is about 720 × 1044: **about twice the area**, text about 1.45 times bigger. After this WP the reader has an
orientation setting (Landscape / Portrait, turned left / Portrait, turned right) and a quick button to flip it, and everything
the reader shows (page, spinner, messages, page number, menu) is drawn upright for the way the console is held, with touch
and the D-pad following. WP-13 (webtoon + zoom) then builds on this and must work in both orientations.

## Decided in planning (2026-10-30, defaults the owner can still change)
The owner didn't pick from the offered options, so these are the planner's recommended defaults:
- **Reader only.** Library, manga details and settings stay landscape: you turn the console once a chapter is open.
  Rejected for now: the whole app in portrait. borealis has no rotation (its `Application::frame` only scales) and lays every
  screen out 1280 units wide, so that needs a borealis patch and doubles every screen's tests and goldens (L+, risky).
- **Chosen by hand, not by the motion sensor.** A reader setting, one for all manga (it's about how you hold the console, not
  about the manga), saved in config.json as `reader.orientation`: `landscape` (default) / `portrait-left` / `portrait-right`.
  Plus a quick flip while reading: **Y** (check it is free in the reader) and a "Rotate" item in the reader's menu bar, which
  flip between landscape and the portrait way used last (the first time: turned left). Auto-rotate from the motion sensor is
  an idea for later (it would also need a lock, or it turns while you read lying down); not in this WP.
- **Docked = landscape.** On the TV the reader is always landscape; the saved choice comes back in handheld mode, also when the
  console is docked or undocked in the middle of a chapter.
- Names: **turned left** = hold the console normally, then give it a quarter turn to the left (counter-clockwise): the left
  Joy-Con ends up at the bottom. **Turned right** = a quarter turn to the right: the right Joy-Con at the bottom.
- Record these as a new decision (the next free D number) when the WP is done, with what the owner said after trying it.

## Read first
`docs/STATUS.md`; D16 (touch first) and D44 (the paged reader) in `docs/DECISIONS.md`; `src/core/ReaderMath.hpp`,
`src/core/AppConfig.hpp`; `src/ui/README.md`, `src/ui/reader/*.hpp` (this WP changes `ReaderView.cpp` and
`ReaderActivity.cpp` too, so read those); `src/platform/Platform.hpp`; `tests/e2e/README.md` and `tests/e2e/lib/reader.ts`.
From borealis only: `View::hitTest` and the focus highlight in `library/borealis/library/include/borealis/core/view.hpp`, and
`Application::frame` in `lib/core/application.cpp` (how a frame is drawn and where the highlight is drawn).

## How (the planner's suggestion; the session may find a better way)
1. **Math in core (pure, unit-tested).** A `ReaderOrientation` enum with names in `AppConfig` (like `PageFit`). In
   `ReaderMath`: the reader's screen for an orientation (portrait = width and height swapped: 720 × 1280 units in handheld), a
   screen point → reader point mapping and the same for a movement (swipes, drags), and what a D-pad direction means in each
   orientation. The existing rules (`fitPage`, `clampPosition`, `startPosition`, `canMoveToward`, `tapArea`, `swipeSide`)
   stay as they are: they just get the portrait screen and mapped points. `decodeBoxFor` gets the swapped real-pixel screen,
   so portrait pages decode at their portrait size.
2. **Drawing.** In portrait, `ReaderView` draws everything (page tiles, spinner, and its children) inside one NanoVG
   transform (translate + rotate by a quarter turn, one way or the other), with its children laid out for the portrait screen.
   This is our code; borealis is not changed.
3. **Touch.** borealis reports touches in landscape screen coordinates and finds the touched view by its unrotated box. So in
   portrait `ReaderView` takes every touch (as it does today with its tap and pan recognizers), maps it with the core math and
   decides itself: a tap zone, a swipe, a drag, or one of the reader's buttons (menu bar, Retry: it checks the mapped point
   against those buttons' portrait boxes and runs their action). No borealis tap may land on a button at its unrotated place.
4. **Controller.** The D-pad and the sticks turn with the screen: the press that points to the screen's right, as you now hold
   the console, counts as right, and the one pointing to the screen's top scrolls up. L/R keep their meaning (L = left side,
   R = right side, mirrored for right to left as today): after a quarter turn both shoulder buttons sit on the same side of
   the console, so a "physical" mapping doesn't help. The owner tries it and the session adjusts.
5. **The rest of the reader.** Labels and bars use the reader's own screen size, not `Application::contentWidth`. The
   "previous / next chapter" screens, errors, Retry, page number and menu are all drawn upright. The reader settings menu
   (a borealis dropdown) still opens landscape: acceptable, since Y covers the common case; note it in the README.
6. **Docked.** A new platform call, e.g. `platform::handheldMode()`: Switch `appletGetOperationMode() ==
   AppletOperationMode_Handheld` (read-only; the safety gate must still PASS), desktop: true unless the new desktop test flag
   `--simulate-docked`. Checked when the reader opens and while it runs (cheap enough each frame? else once a second).
7. **Spreads.** A two-page spread in portrait is shown whole, so it is small (720 × about 500). Reported as today. Turning a
   spread back to landscape by itself is an idea for later (WP-13 or after), not this WP.

## Deliverables
- [x] **Spike first** (desktop, short; done as the real ReaderView instead: `reader-portrait` checks the same: upright both
      ways, buttons work by touch where you see them, the focus stays on the page): a rotated `ReaderView` with one label and one button. It is drawn upright both ways,
      a tap on the button works where you see it, and borealis's focus highlight never shows at the unrotated place (the
      focus stays on the page). If borealis fights this, stop at a clean point and write what you found in STATUS before
      building the rest.
- [x] core: `ReaderOrientation` + names, `reader.orientation` in config.json (old files lack it: landscape, no migration),
      the rotation math. Unit tests: both portrait ways, point and movement mapping (and back), D-pad meaning, fits on
      720 × 1280 for a typical page (1 : 1.45 → 720 × 1044), a spread, decode boxes
- [x] `ReaderView`: rotated drawing with children laid out for portrait; touch mapping; buttons run by mapped taps
- [x] `ReaderActivity`: layout from the reader's screen size; D-pad and sticks turned; Y and the menu's "Rotate"; the
      Orientation setting in the reader settings; landscape when docked, the choice back when undocked
- [x] platform: `handheldMode()` on both platforms, desktop flag `--simulate-docked`; Switch build's safety gate PASS
- [x] Test events: the `reader` event gains `orientation` (what is in effect) and the saved choice; page sizes in the
      reader's own units. E2E helper that turns a portrait point into the landscape touch point the drivers send
- [ ] Scenario `reader-portrait` (desktop and Switch; **desktop green**, the Switch run is left; it also does the 100 turns): Alpha ch1, Y to portrait, goldens of the page, the menu and the end
      screen, both ways; edge taps, a swipe, a drag on a fit-width page, the menu's Close by touch, the turned D-pad; the
      setting survives a restart; `--simulate-docked` shows landscape (desktop only). The existing reader scenarios stay
      landscape and unchanged
- [ ] Console (application mode, handheld, stock clocks): 100 prefetched turns in portrait (p90 < 150 ms, same rule as WP-11),
      memory flat (growth < 16 MiB after turn 20)
- [ ] Owner tries both ways in their hands: which way, whether the buttons and tap zones feel right; adjust
- [x] Docs (D46; the owner's verdict still to add): new decision in DECISIONS.md, ARCHITECTURE.md reader math line, TESTING.md scenario, `src/ui/README.md`, the user
      part of README.md (how to read in portrait), CHANGELOG.md (whether this is v0.2.0 is the owner's call). WP-13's stub in
      ROADMAP.md: say it must work in both orientations

## Acceptance
`tools/build.sh tests` green (with the new unit tests). `tools/e2e.sh desktop` green for every scenario.
`tools/e2e.sh switch reader-portrait` and `reader-paged` green in handheld mode. Portrait page turns on the console: p90 under
150 ms when prefetched, memory flat across 100 turns. The owner has read a real chapter in portrait on their server and is OK
with how it feels.

## Sonnet-safe chunks
- The docs updates at the end (from a list of facts Opus writes).
- Refreshing goldens after an intended look change (the harness diffs, Sonnet re-runs).
- Not the rotation math, the touch mapping or `ReaderView` (the reader is never Sonnet, ROADMAP).
