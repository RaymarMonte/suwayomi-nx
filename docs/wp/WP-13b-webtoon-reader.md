# WP-13b Webtoon reader (the vertical strip)

Model: **Opus** · Size: L (split point below: zoom in the strip can move to WP-13d) · Needs: WP-13 (and WP-13c, done) ·
Owner: try it in your hands (a real webtoon from your server, both orientations: the sticks, the D-pad, a flick, taps, a
chapter change, coming back to a chapter, zoom); console

Planned 2026-11-10 (planning session, owner's answers below). Runs **next**, before WP-18 (downloads), so downloaded
chapters work in the strip from the start (D47). It reuses WP-13's zoom math, two-finger handling and touch channel, WP-22's
portrait transform (D46) and WP-07's tiles (D39).

## Goal
Today a webtoon (a long-strip comic, like most manhwa) can only be read page by page: a strip of 800 × 12000 pixels is shrunk
to fit the screen (about 48 × 720 in landscape), and a chapter cut into short slices turns one slice at a time. After this WP
the reader has a third reading direction, **Vertical strip**: all pages of a chapter join into one continuous strip that
fills the screen's width (half of it in landscape), and you scroll it smoothly with a stick, the D-pad, L/R, taps, or by
dragging and flicking it with a finger. The next chapter joins on below and the previous one above, so you just keep
scrolling. Only the part of the strip near the screen is on the GPU (S7), so a chapter of any length stays within memory. A
manga that looks like a webtoon opens as a strip by itself. Progress is the page at the screen's middle, and the exact spot
inside that page is saved on the server too, so coming back opens where you stopped. The strip has its own orientation (held
sideways at first) and zooms with WP-13's pinch, double tap and ZL/ZR.

## The owner's answers (2026-11-10)
- **Opens as a strip by itself** when you haven't picked a reading direction for that manga yet and it looks like a webtoon:
  the server tags it "Long Strip" or "Webtoon", or its first page is more than 2.5 × as tall as it is wide. You can always
  switch in Reader settings → Reading direction (Left to right / Right to left / **Vertical strip**); the choice is
  remembered per manga (D44).
- **Strip width in landscape: half the screen** (640 of 1280 units), so text is about as big as in portrait (an 800-wide
  strip is drawn at 0.8 × in landscape, 0.9 × in portrait); you see a shorter piece at a time. A new setting "Strip width"
  offers **Half** (default) / **Three quarters** / **Full**. In portrait the strip always fills the width.
- **Taps: top / bottom scroll.** A tap in the lower part of the screen moves on about a screen, in the upper part goes back,
  the middle opens the menu; a double tap in the middle zooms (as in the page reader).
- **Chapter end: keep scrolling.** The next chapter is joined on below with a short band ("Finished: Chapter 5 · Next:
  Chapter 6"); scrolling up past a chapter's start joins the previous one the same way. Like Mihon.
- **Coming back: the exact spot, saved on the server.** The server only stores the page number (`lastPageRead`); the app
  also saves how far down that page you were, as a small note on the chapter (Suwayomi's chapter meta; other apps ignore
  it). It survives a reinstall, and WP-18b's offline queue can carry it.
- **The strip remembers its own orientation.** The page reader keeps its own (D46); the strip starts held sideways, the way
  the console was turned last, and Y flips either one. This changes D46's "one orientation for all manga" (owner's OK,
  2026-11-10).

## Planner's defaults (the owner can change them after trying; record them all in a new decision D49 at the end)
- **The strip:** the pages of a chapter touch with no gap (webtoon slices are cut to join), each drawn at the strip's width,
  its height from its shape. In landscape the strip is centred; the sides show the reader's background colour. A spread in
  the strip is drawn whole at the strip's width (no splitting). A page not loaded yet gets the height of its chapter's last
  known page shape (1.5 × the width before any is known) and shows the spinner. A page that failed shows a band one screen
  tall with the reason and "Tap or X to retry"; the strip goes on below it.
- **The chapter band:** a third of the reader's screen tall, between two chapters: "Finished: <chapter>" and "Next:
  <chapter>" (the server's chapter names). Above the first chapter: "No previous chapter"; after the last: "No more chapters",
  where the strip ends. At most three chapters sit in the strip (the one being read and its neighbours); one further away is
  cut out, and its pages let go.
- **Where you are:** the page under the screen's **middle line** (half way down the reader's screen) is the page being read:
  the page number shows it, `lastPageRead` saves it, and its chapter is the chapter being read. When the middle line moves
  into a page of another chapter, that one becomes the chapter being read: everything of the old one is saved at once (D44's
  "leaving a chapter") and the bars show the new title. A band under the middle line changes nothing. A chapter is read
  (`isRead`) at once when the bottom of its last page comes on screen. Re-reading never un-reads (D44).
- **The spot:** the middle line's place inside that page, as a share of the page's height (0 = its top, 1 = its bottom),
  saved together with the page in one request: `updateChapter` and `setChapterMeta` (key `snx_strip_spot`, value
  `"<page>:<share>"`, e.g. `"2:0.4375"`) in one mutation document. Checked on the test server (v2.4.2366, 2026-11-10): both
  fields answer in one request, `chapters { nodes { meta { key value } } }` and `chapter(id) { meta }` read the note back,
  and setting it again overwrites it. It goes with every progress save in the strip (still at most every `progressSyncMs`,
  and everything at once when leaving a chapter, at HOME/sleep and on close; never cancelled, D44). Opening a chapter as a
  strip: the reader reads `meta` from `fetchChapterPages`'s chapter (a `ChapterType`, so the same field). If the note's page
  is the server's `lastPageRead`, that spot goes under the middle line; otherwise (another app moved on) the top of the
  server's page goes to the screen's top. A read chapter opens at its start (D44). The page reader ignores the note.
- **"By itself":** the genre rule: a tag that is "Long Strip", "Webtoon" or "Webtoons" (any case, the whole tag). The shape
  rule: the first page decoded for the reader (the page it opens at) is more than 2.5 × as tall as wide (its source size);
  it decides once, before that page is drawn, and the page loads again for the strip. "Manhwa" and "Manhua" don't count
  (older ones are paged; the owner can ask for them after trying). A choice made by itself is saved as the manga's own
  (`reader.manga`), so the manga opens as a strip at once next time and never flips while you read; the `reader` event says
  which rule made it.
- **Touch:** a drag scrolls (sideways too when zoomed). Let go while moving and the strip **flicks** on and slows down: the
  speed comes from the finger's last ~100 ms (measured by us: borealis's pan end assumes every frame takes 1/60 s and slows
  by a fixed 3000 units/s², `pan_gesture.cpp`), then halves about every 0.25 s, stops below ~20 units/s, and starts at most
  at ~5 screens a second. A touch during a flick stops it (that touch is not a tap). No swipe turns anything in the strip.
  Taps, on the reader's screen (so turned in portrait): the top 30 % glide back 0.75 of a screen, the bottom 30 % glide on
  0.75, the middle 40 % open the menu (after the 250 ms double-tap wait, D48). With the menu open any tap closes it (D44).
- **Controller** (turned with the screen, `core::readerArrow`; L/R keep their sides, D46): D-pad up/down glide 0.75 of a
  screen in 0.25 s (eased out; a bit slower than the zoomed page's 0.15 s so the eye can follow the text); held past 0.3 s
  the strip scrolls on smoothly at one screen a second until let go. L and R do the same as up and down, so one hand works in
  any grip. Either stick scrolls smoothly, faster the further pushed (WP-13's 15 % dead zone and curve; a full push crosses
  one screen in 0.6 s). Left/right (D-pad or stick) pan sideways only when zoomed, else nothing. ZR/ZL zoom steps, A the
  menu, B closes, X retries the failed page nearest the middle, Y rotates (the strip's own orientation), MINUS settings.
- **Zoom in the strip** (WP-13's math, with the strip as the page): 1 = the strip's width; the pinch, the middle double tap
  (2 ×, again: back to 1) and ZR/ZL (1, 1.5, 2, 3, 4) up to `maxZoom`, the point under the fingers (or the screen's middle)
  stays put. Zoomed, the strip is wider than the screen and pans sideways as well. The zoom stays while scrolling and across
  chapters; it goes back to 1 on Rotate, on a strip-width change and when the reader closes. Sharper pages: 150 ms after the
  zoom and the scroll have both settled, the pages on screen (at most two) decode again at the zoomed box (`zoomDecodeBox`
  with fit Width at the strip's width, capped as in D48); the strip-width decode stays drawn until the sharper one is, and a
  page that scrolls away lets its sharper decode go. Most webtoon strips are 720–1080 pixels wide, so above about 1.1–1.5 ×
  they have no more detail and nothing decodes again (WP-13c's "no sharper" rule).
- **Settings in strip mode:** Reading direction, Strip width (in place of Fit), Background, Page number, Orientation (the
  strip's own). "Zoom on page turn" and "Split spreads" don't show (no page turns, no halves). Strip width and the strip's
  orientation are for all manga, like the background: new keys `reader.stripWidth` (`half` / `three-quarters` / `full`)
  and `reader.stripOrientation` (missing = `reader.lastPortrait`, the way the console was turned last; Y in either mode
  updates `lastPortrait` as now). Old files lack them: no migration. Docked is landscape (D46).
- **Switching** the direction to or from Vertical strip keeps the page being read: the strip opens with that page's top at
  the screen's top; the page reader opens the page under the middle line.
- **Unchanged:** the paged reader (left to right, right to left), its settings, its orientation and its scenarios.

## Read first
`docs/STATUS.md`; D16, D36, D39, D44, D46, D48 in `docs/DECISIONS.md`; `src/core/README.md`, `src/core/ReaderMath.hpp`,
`ImageGeometry.hpp` (`planTiles`, `tileWindow`), `AppConfig.hpp`, `RuntimeProfile.hpp`, `ProgressSync.hpp`,
`models/Chapter.hpp`, `models/PageList.hpp`, `models/ReadProgress.hpp`, `models/Manga.hpp` (`genre`),
`graphql/Operations.hpp`, `graphql/FetchChapterPages.graphql`, `UpdateChapterProgress.graphql`; `src/api/README.md`,
`SuwayomiClient.hpp` (`setProgress`); `src/image/README.md`, `ImageLoader.hpp`, `Caches.hpp`, `DecodedImage.hpp`;
`src/ui/README.md` and all of `src/ui/reader/` (this WP changes them, so read the `.cpp` files too); `src/app/TestHooks.hpp`
and `.cpp` (the touch channel must reach the strip); `tests/e2e/README.md`, `tests/e2e/lib/reader.ts`, `lib/touches.ts`,
`lib/pagecode.ts` (it reads Gamma's segment codes from a scaled window of the strip: `pagecode.test.ts`), `lib/screens.ts`;
TESTING.md "Fixture manga" and scenarios 6b–6e. From borealis only: `lib/core/touch/pan_gesture.cpp` (why we measure the
flick ourselves) and how a `Label` draws its text with NanoVG (the font call, for the chapter bands).

## How (the planner's suggestion; the session may find a better way)
1. **Math in core (pure, unit-tested),** in a new `core/StripMath.{hpp,cpp}` (`ReaderMath` is long already).
   - `StripLayout`: the strip as a list of items top to bottom (bands, each chapter's pages), each with a height in units at
     zoom 1, and the scroll position `top` (the strip's y at the reader screen's top), in `double` (three long chapters are
     10^5–10^6 units). `contentHeight`, `clampTop` (no gap above the first band or below the end band; a strip shorter than
     the screen sits at the top), `itemAt(y)`, `visible(top, screenHeight)`, `middle()` → (page, share), `topFor(spot)`
     (the share of a page under the middle line, or a page's top at the screen's top), and `setHeight` / `insertAbove` /
     `insertBelow` / `removeChapter` that keep what is under the middle line still: a change above it moves `top` by the
     same amount, a change below it moves nothing. `pageHeight(source, stripWidth)` and the estimate for unknown pages.
   - `stripWidth(StripWidth, orientation, readerScreen)` (half / three quarters / full of the landscape reader screen's width;
     full in portrait) and the decode box `{the strip's width in real pixels, 0}` (the loader's cap and 15 % slack apply,
     D39: an 800-wide strip stays 800 in portrait, WebP scales straight to 640 in landscape). Zoomed: `zoomDecodeBox(Width,
     ...)` at the strip's width.
   - The tile window across pages: for each page near the screen, which rows of its decoded tiles (`planTiles`' order)
     overlap [screen top − `webtoonTilesAround` rows, screen bottom + `webtoonTilesAround` rows] (`tileWindow` with the
     page's place and scale), and the upload order: tiles on screen first, then nearest first.
   - The prefetch window: the pages overlapping [top − `stripScreensBehind` screens, bottom + `stripScreensAhead` screens]
     (new profile fields, below), the page under the middle line first, then ahead nearest first, then behind; and when the
     window reaches a chapter's band (the neighbouring chapter's page list is wanted).
   - Fling and scroll: `releaseVelocity(samples, nowMs, windowMs = 100)` (the finger's places with their times),
     `flingOffset(velocity, seconds)` and `flingDone(velocity, seconds)` (half-life 0.25 s, stop speed, top speed), the held
     D-pad's speed and the 0.75-screen, 0.25 s glide (`glideProgress` exists). Named constants, like `kArrowGlideMs`.
   - Taps: `stripTapArea(y, height)` → Back / Middle / On (30 / 40 / 30 %); the double tap keeps `isDoubleTap`'s gap and
     distance, in the middle band.
   - The spot note: `stripSpotValue(page, share)` → `"2:0.4375"` and `parseStripSpot(text)` (tolerant: bad text gives
     nothing; the share kept within 0..1; S6: at most 32 characters).
   - "By itself": `looksLikeStrip(genres)` and `isStripPage(source)` (> 2.5 × as tall as wide).
   - `AppConfig`: `StripWidth` with names, `reader.stripWidth`, `reader.stripOrientation` (optional), and a way to tell that a
     manga has its own direction (today `readerSettingsFor` gives the defaults either way). `ReadingDirection::Vertical`
     and its name `vertical` exist (D36). `ProgressSync` gains the spot: a moved share counts as something to save (the same
     throttle); `ReadProgress` gets an optional spot.
2. **GraphQL and api.** `FetchChapterPages` asks for `chapter { ... meta { key value } }`, and the reader's chapter gets the
   parsed spot. A new document `UpdateStripProgress($id, $patch, $spot)` = `updateChapter` + `setChapterMeta(input: {meta:
   {chapterId: $id, key: "snx_strip_spot", value: $spot}})` in one request; `SuwayomiClient::setProgress` takes an optional
   spot and sends that document when it has one (a mutation: never retried by net, D38; never cancelled, D44). Contract test
   (`npm run contract`) and the recorded answers again (`tools/record-fixtures/record-all.sh`).
3. **The model.** A new `StripModel` in `src/ui/reader/` (`snx_ui_models`, no borealis, unit-tested like `ReaderModel` with
   `FakeHttpClient` and a real `ImageLoader`): up to three chapters' page lists and slots, the `StripLayout`, the prefetch
   window by screens, the neighbouring chapters' page lists, one `ProgressSync` for the chapter being read and the chapter
   change and read rules, the open spot, the "by itself" shape result, and the zoomed decodes of the pages on screen. Lift
   `ReaderModel::Saver` into a small shared class both models use, so saves outlive the reader in both modes (D44).
   `ReaderModel` otherwise stays as it is (its tests are the guard); it reports the first decoded page's source size, so the
   activity can switch to the strip by the shape rule. If one model with two modes turns out simpler, fine, as long as
   `ReaderModel`'s existing tests stay unchanged.
4. **The view.** `ReaderView` already has what the strip needs around the page: the portrait transform and overlay, the
   culling fix, the mapped buttons, finger counting and the pinch recognizer, tap and double-tap timing, glides from the
   clock. Suggested: move those into a shared base (`ReaderSurface`, say) with `ReaderView` (one page, as now) and a new
   `StripView` on top, rather than a second copy of the touch code. `StripView` draws the visible pages' tiles at the
   strip's place (one texture per tile, each reserved in `TextureBudget`, S7), keeps the textures of the tile window,
   uploads the tiles on screen at once and the others at most one per frame (nearest first), frees the ones that leave (if
   the budget refuses, the farthest go first), draws the placeholders, the bands (NanoVG text with borealis's font) and the
   failed-page band with its retry, and runs the flick, glides and the held scroll from the clock. The scroll position lives
   in the core layout, so a page arriving above the screen never moves what you see. The existing scenarios
   (`reader-paged`, `reader-portrait`, `reader-zoom`, `reader-spread-split`, `touch-pass`) guard the split and stay unchanged.
5. **The activity.** `ReaderActivity` hosts either the paged pair (`ReaderModel` + `ReaderView`) or the strip pair
   (`StripModel` + `StripView`) and switches when the direction changes (by hand or by itself); the menu, bars, messages,
   settings dropdowns, controller polling and events stay in one place (no second copy of the settings menu). In strip mode:
   the controls above, Y and the Orientation setting act on the strip's own orientation, the settings show their five
   entries. The touch channel (`touches`) plays into whichever view is shown. The screen stays on as now.
6. **Profile:** `stripScreensBehind` / `stripScreensAhead` (Full 1 / 3; safe mode and applet 0 / 1). `webtoonTilesAround`
   (Full 4, safe mode 1) gets used for the first time. Flick and scroll feel are named constants in core, not profile limits.
7. **Test hooks and events.** `reader` gains `mode` (`paged` / `strip`), and in strip mode (written when the scroll has
   settled and on every chapter change) `chapterId`, `page` (under the middle line), `spot`, `top`, `stripW` (units),
   `zoom`, `auto` (`genre` / `shape`, when the strip was chosen by itself), `pagesHeld`, `tiles`, `texUsed`, `waitFrames`
   (frames since the last event in which part of the screen showed a page still loading), `openMs` (the reader opening to
   the first frame that draws the strip); `progress` gets `spot`; `gesture` gets the kinds `scroll` (stick, held D-pad or
   L/R), `fling`, `glide` and `drag`. E2E helpers: `flick()` in `lib/touches.ts` (frames of a fast drag, then the lift) and
   a check in `lib/reader.ts` that reads the colour code at the screen's middle (series, chapter, page, segment). Anything
   the clock drives (flick, glide, held scroll) is asserted as a range on the settled event; played drags exactly.

## Deliverables
- [x] (Folded into the build: the console run answered it. Frames that upload 800 × 2000 tiles stay within 20 ms, so no
      smaller tiles; D49.) **Spike first** (short): on desktop, Gamma ch1's three strips joined and scrolled by the stick, with the tile window
      and one upload per frame; on the console (application mode, handheld, portrait), a full-push stick scroll through it:
      frame intervals and the upload time of one 800 × 2000 tile (6.1 MiB; WP-13c measured ~1 ms per MiB). If a tile upload
      doesn't fit in a frame next to the drawing, plan smaller strip tiles before building the rest, and write what you found
      in STATUS.
- [x] core: `StripMath` (layout and anchor, strip width and boxes, tile window across pages, prefetch window by screens,
      fling and held scroll, strip tap zones, the spot note, the two "by itself" rules), `StripWidth` + config keys, the
      strip orientation key, "has its own direction", `ProgressSync` / `ReadProgress` spot. Unit tests: a height change above
      the middle line keeps the view still, below it moves nothing; chapters added above and below and cut out; `clampTop` at
      both ends and for a short strip; `middle()` and `topFor()` round trip (spot → top → spot); the tile window at seams and
      across a band; the prefetch order; the flick's distance and stop, the release speed from samples (fast, slow, a finger
      that stopped before lifting); tap zones; spot text round trip and bad text; genre and shape rules; strip width per
      setting × orientation; config round trip and old files; `ProgressSync` saves a moved spot within the throttle
- [x] GraphQL/api: `FetchChapterPages` with `meta`, `UpdateStripProgress`, `setProgress` with a spot; contract test green,
      recorded answers refreshed, a unit test that the spot request carries both fields
- [x] `StripModel` (suite `ui`; its zoomed decodes: WP-13d): opens at the spot (and at the page's top when the note's page differs, at the start when
      read); the window loads the pages near the screen and lets the far ones go; the next chapter joins below near the end
      and the previous one above near the start (view still); a fourth chapter cuts the farthest out; the chapter being read
      changes at the middle line, the old one saved at once; `isRead` when the last page's bottom shows; progress with the
      spot; a failed page retries; the zoomed decode only for the pages on screen, let go when they leave; `ReaderModel`'s
      tests unchanged
- [x] Views (zoom and the sideways pan: WP-13d; strip mode inside `ReaderView` + `StripCanvas`, D49): the shared base (or the session's better shape), `StripView` (tiles, window, uploads, placeholders, bands, the
      failed band, flick, glides, held scroll, zoom and sideways pan), the `touches` entry for tests
- [x] `ReaderActivity`: the strip pair, switching by hand and by itself (shape: before the first page is drawn), the strip
      controls, the strip's own orientation, the settings menu in strip mode ("Vertical strip" in Reading direction, "Strip
      width"), the new event fields and gesture kinds
- [x] Profile: `stripScreensBehind` / `stripScreensAhead` (unit test of the profile table)
- [x] Test hooks: `flick()` and the middle-code check in `tests/e2e/lib/` (with vitest tests), TESTING.md says how
- [x] (Its zoom steps: WP-13d.) Scenario `reader-webtoon` (desktop and Switch). Desktop: Gamma ch1 from the details screen with no own choice → strip
      by itself (`auto: shape`), held sideways (portrait-left on a fresh config), `stripW` 720, the strip's top at the top,
      config.json then has Gamma as `vertical`; a played drag moves `top` by the drag; a played flick carries on past the
      finger and stops; a touch during a flick stops it; a bottom tap glides on ~960 units, a top tap back, a middle tap opens
      the menu after the wait; the code at the screen's middle is the expected segment for `top`; a golden across the seam
      of strips 1 and 2 (no gap); after scrolling into page 2 the server has `lastPageRead` 1 and the spot note; at the end of
      ch1 the band shows (golden), ch1 is read on the server, ch2 follows, `chapterId` changes; scrolling up from ch2's start
      shows ch1's last strip; closing half way down ch2's page 2 and opening ch2 again (Continue) puts the same segment
      under the middle line; Alpha ch1 meanwhile opens paged in landscape (the strip is in portrait: each keeps its own
      orientation); in the strip, Rotate → landscape, `stripW` 640 centred (golden), "Strip width: Full" → 1280, and Gamma
      opens in landscape next time (remembered); Alpha switched to
      "Vertical strip" by hand reads as a strip of its 1000 × 1500 pages at the same page, and back to "Left to right" shows
      the page under the middle line; in the strip a double tap zooms to 2 × at the point, a pinch to 3 and back, a zoomed drag
      pans sideways, scrolling keeps the zoom. Switch adds: stick scrolling (`/input/stick`, full and half push), the held
      D-pad and L/R, the frame numbers and the soak below
- [x] (Changed only: the direction menu's third row in `reader-paged` / `reader-spread-split`; `reader-zoom`'s portrait
      golden now before the console's swipe, which varies by a step.) The existing scenarios stay green and unchanged: `reader-paged`, `reader-rtl-spread`, `reader-portrait`, `reader-zoom`,
      `reader-spread-split`, `resume-progress`, `touch-pass`, `broken-images`, `soak` (and the rest of `tools/e2e.sh desktop`)
- [x] Console (application mode, handheld, stock clocks, D17): the numbers in Acceptance; the Switch build's safety gate PASS
- [x] (2026-11-12: "Feels good"; coming back to a re-read chapter stays as it is, D49.) Owner tries it in their hands: a real webtoon from their server in both orientations (stick, D-pad, a flick, taps, a
      chapter change, coming back to a chapter, zoom), and whether the strip picks itself for the right manga in their library
- [x] (The owner's verdict goes into D49 after their test.) Docs: D49 (the answers, the defaults as they ended up, the D46 change, the console numbers, the owner's verdict),
      ARCHITECTURE.md (reader math line, the "Webtoon tiles kept decoded" row and the new prefetch fields), TESTING.md
      (scenario 6f, the new events and helpers), SUWAYOMI-API.md (the chapter meta note and the two-field mutation),
      `src/core/README.md`, `src/api/README.md`, `src/ui/README.md`, `tests/e2e/README.md`, the user part of README.md (how to
      read a webtoon; "Not there yet" loses the webtoon reader), CHANGELOG.md (a version is the owner's call)

## Acceptance
`tools/build.sh tests` green with the new unit tests; `npm test`, `npm run typecheck` and `npm run contract` green.
`tools/e2e.sh desktop` green for every scenario. `tools/e2e.sh switch reader-webtoon`, `reader-paged`, `reader-portrait`,
`reader-zoom` and `reader-spread-split` green in handheld mode. On the console (application mode, handheld, stock clocks), Gamma
Webtoon (WebP 800 × 12000 strips) from the test server:
- **Smooth:** while the strip scrolls (a full stick push for 3 s, a half push, the held D-pad, a played flick, across a seam
  between strips and across a chapter band), frame intervals p90 ≤ 17 ms (60 frames a second) and none over 34 ms, the
  frames that upload tiles included.
- **No waiting once cached:** from the second pass on, `waitFrames` is 0 at a full stick push; on the first pass (from the
  SD-card cache) the strip ahead is decoded before it comes on screen (WP-07: 176 ms per strip), so only the opening shows a
  spinner. Opening: `openMs` ≤ 0.5 s for a manga already saved as a strip, ≤ 0.8 s the first time (by shape: the first page
  decodes twice).
- **Memory flat:** three passes down and up through both chapters (stick, full push) grow memory by < 16 MiB after the first
  pass; `texUsed` stays ≤ 64 MiB (the tiles on screen plus four rows of 800 × 2000 tiles above and below); no `mem_pressure`,
  no `error` event, no crash report.
- **Progress:** the server's `lastPageRead` and spot note follow the middle line within `progressSyncMs`; a chapter is read
  once its end is on screen; opening it again puts the same segment code under the middle line.
The owner has read a real webtoon in both orientations and is OK with how scrolling, flicks, taps and the chapter change feel.

## Split point
If the session runs long: the strip itself (core math, `StripModel`, `StripView`, scrolling, flicks, taps, the bands and
joined chapters, the spot on the server, "by itself", strip width, the strip's orientation, `reader-webtoon` without its zoom
steps, the console numbers) lands first, green. Zoom in the strip (the pinch, the double tap, ZR/ZL, the sideways pan and the
sharper decodes of the pages on screen) then moves to a new **WP-13d** in ROADMAP.md (Needs 13b, before WP-18), and STATUS
says so. Until then a middle tap in the strip opens the menu at once (no double tap to wait for).

## Sonnet-safe chunks
- The docs updates at the end (from a list of facts Opus writes), the i18n strings.
- Refreshing the recorded answers and the contract test after the `FetchChapterPages` change (`record-all.sh`, mechanical).
- The `flick()` frame builder and the middle-code check in `tests/e2e/lib/` (pure TypeScript from a spec plus vitest tests);
  refreshing goldens after an intended look change.
- Not `StripMath`, the flick, `StripModel`, `StripView`, the uploads, the view split, the progress and spot saves or the
  controller rules (the reader and the image pipeline are never Sonnet, ROADMAP).

## Not in this WP
Gaps between pages (a setting for regular manga read as a strip, if the owner wants one after trying); a "paged vertical" mode
(pages that turn up and down); hands-free auto-scroll; counting "Manhwa" / "Manhua" tags for "by itself"; flick momentum for a
zoomed page in the page reader (core's fling math makes it a small follow-up; the owner hasn't asked); cropping white borders;
splitting wide images in the strip; reading downloaded chapters (WP-18 makes both readers read from the SD card) and keeping
the spot in the offline queue (WP-18b).
