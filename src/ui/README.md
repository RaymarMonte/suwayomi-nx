# ui

**Purpose:** the borealis screens (ARCHITECTURE.md "ui", D40). Touch first (D16): everything works with a tap, and with a controller.

**The screen pattern** (copy it for every new screen):
- `src/ui/<screen>/<Name>Activity.{hpp,cpp}`: the view. Layout in `resources/xml/activity/<name>.xml`, views bound with `BRLS_BIND`,
  texts from `resources/i18n/en-US/<screen>.json`. It takes `app::AppContext&` (everything shared: client, images, textures,
  config, profile).
- `src/ui/<screen>/<Name>Model.{hpp,cpp}`: the state and rules, **no borealis**, so the unit tests (suite `ui`) test it.
  Part of the library `snx_ui_models` (`src/ui/CMakeLists.txt`); the views are compiled into the app (`src/app/CMakeLists.txt`).
- Every network call takes the screen's `net::CancelToken`; the destructor cancels it; a callback checks `token.cancelled()` first.
- A screen reports itself with `app::testhooks::screen("<Name>")` and shows errors with `StatusView::showError` (writes `error`).
- Touch targets ≥ 72 px: cells `minHeight="72"`, buttons `height="72"`.

**How to make a list screen** (WP-09, D42; the Library tab is the example, WP-14/15 follow it):
1. A model in `snx_ui_models` that calls `SuwayomiClient` itself: `start()`, `refresh()`, a `State` (Loading / Ready / Empty /
   Failed + `error()`), `items()`, and `showing(lastIndex)`, which asks for the next page near the end. One listener
   (`setListener`) that the view sets and clears. Keep one `CancelToken` for the request under way; every callback checks
   it first (the model may be gone). Test it with `FakeHttpClient` answers built in the test (`tests/unit/test_ui_library.cpp`),
   `hold(true)` for "still loading" and for "destroyed while loading".
2. If the list must survive a tab change, the main screen owns the model (`MainActivity::library_`); the tab's view only
   attaches to it. Remember the focused item in the model (`rememberFocus`), and give it back to `CoverGrid::reload`.
3. The view: a `StatusView` (loading, error + Retry, empty) and a `CoverGrid` (or another `GridSource` user) in the same box,
   one of them visible (`showGrid` in `LibraryTab`: move the focus off the one being hidden, never delete a focused view).
   Implement `GridSource`: `makeCell` (a `GridCell` subclass, all fixed sizes), `bind` (fill it, start its image load at
   `Interactive` priority with the cell's own token), `unbind` (cancel, `image->clear()`, drop the `TextureLease`), `select`.
   Images: `images.cached()` first, else `images.load()`; textures via `nvgCreateImageRGBA` after a `TextureLease` on
   `context.textures` (S7), one tile per cover.
4. Buttons: actions on the tab itself (`registerAction`: they work while any child has the focus) plus a visible button for
   each (D16). PLUS is "quit" everywhere: a screen menu goes on MINUS (D42). A dropdown: `new brls::Dropdown(...)` in a
   `new brls::Activity`; open a second one from the first's callback with `brls::sync`.
5. Test events: one when what the screen shows changes (like `library`), and the grid's `grid` range. A scenario then waits
   for those, never for time (`tests/e2e/scenarios/library-browse.test.ts`; touch coordinates in `tests/e2e/lib/screens.ts`).

**Screens:** `main/MainActivity` (tab frame, always the first screen, the connection check, owns the library model; `SettingsTab`,
`CrashLogsActivity`), `library/LibraryTab` (categories, sort, refresh, the cover grid; WP-24, D54: Search (the search bar takes the tabs' place, B or Clear ends it, `LibraryModel::search` shows the results as the shelf `kSearchShelf`), Edit, Y on a cover = its menu (Open, Categories...), `mangaCategoriesChanged` after a picker save), `manga/MangaActivity` (details + chapter list, WP-10,
D43: `MangaModel` holds the list, sort, filter, Continue and the actions), `reader/ReaderActivity` (the paged reader, WP-11,
D44: `ReaderModel` holds the pages, the prefetch window, chapter changes and the progress saves; `ReaderView` draws the page
from its tiles and turns touch into taps, drags and swipes; full screen, the menu on A or a tap in the middle, settings on
MINUS; `onClosed` runs once the progress is saved, and the details screen reloads its chapters then; WP-22, D46: portrait
with Y / the menu's Rotate / the settings' Orientation: `ReaderView` draws the page and its `overlay()` inside one NanoVG
transform, takes every touch itself in portrait and runs `addButton` buttons by the mapped point, D-pad turned by
`core::readerArrow`; docked is landscape; the reader settings dropdown still opens landscape; WP-13, D48: zoom and pan:
`ReaderView` draws the page `zoom()` times its fit, counts fingers by id with its own first recognizer (borealis's pan
follows one) for the pinch, waits 250 ms after a single middle tap for a double tap, glides from the clock, and swaps in a
sharper image of the same page without moving; `ReaderActivity` reads ZL/ZR, the D-pad and the sticks once a frame (one
press, one page) and, 150 ms after the zoom settles, asks `ReaderModel::setPageZoom` for the sharper decode; WP-13c: a spread
that splits ("Split spreads") is two steps in `ReaderModel` (`half()`, a step back lands on the last half; progress and the
window still count pages), decodes twice as wide once it is known to be a spread (`ReaderPage::reloading` meanwhile), and
`ReaderView::showPage(..., half)` draws only that half of the page's tiles; the settings menu makes borealis's Dropdown tall
enough for its 7 entries; WP-13b, D49: the webtoon strip: `StripModel` holds up to three chapters' pages, the decode
window, the chapter being read (under the middle line) and its progress with the spot; `StripCanvas` draws it inside
`ReaderView` (`setStrip()`, so portrait, the menu, the buttons and touch are shared) from GPU tiles and runs the flick and
glides; WP-13d, D50, zoom in the strip: `StripModel::setView` / `sharpenOnScreen` (the pages on screen decode again
sharper once the view stands still, and let go off screen), `StripCanvas` keeps each page's tiles from one decode and
swaps them when a sharper one is ready (only if they all fit the texture budget), draws the strip zoom × wide from its
left edge and runs the pinch, the double tap, ZR / ZL and the sideways pan; `ReaderActivity` hosts both readers, parks `ReaderModel` while the strip shows, and switches by hand or by itself;
`ProgressSaver` sends both readers' progress), `updates/ChapterRowsTab` (WP-15, D52: one view for the Updates and
History tabs, a one-column `CoverGrid` of rows, the day on the first row of each day; Updates adds the Update library / Stop
header; `ChapterRowsModel` pages the rows, `LibraryUpdater` starts, polls and stops the server's update; MainActivity owns
all three, D42, and reloads the lists when a screen on top closes; B cancels "Opening...", History notes "page N not saved
yet", WP-15b), `reader/WaitingSaves` (WP-15b, D53: the waiting-save queue: progress saves that failed as a reader closed,
`progress-queue.json`, rounds after the main screen's check / any server answer / a back-off timer, the readers' take-over;
no borealis, its clock, timers and test event come in as hooks; tests `test_ui_queue.cpp`), `categories/CategoriesActivity` (WP-24, D54: add, rename, move, delete; Default isn't listed; `CategoriesModel`, a delete asks first), `categories/CategoryPickerActivity` (a manga's categories as a checklist; `CategoryPickerModel` saves only the difference, `chosen()` is the seam WP-14 reuses), `setup/SetupActivity` (server address, login mode, test, warnings), `login/LoginActivity` (ui_login),
`crash/CrashActivity` (the crash screen, S1), `diagnostics/DiagnosticsActivity` (test builds only: WP-05's server check + request
burst, WP-07's image benchmark).

**Components** (`components/`): `StatusView` (loading / error + Retry / message; never takes the focus while hidden),
`TextFieldCell` (XML `<TextFieldCell>`: a tap opens the keyboard; secret fields show dots), `errorText()` (says "no network"
when the console has none), `checkServer()` (version + categories when a login is configured, D40), `CoverGrid` + `GridCell` +
`GridSource` (the recycling grid, D42; one column makes it a list, as on the manga screen; `rebind()` redraws rows in place)
and `GridLayout` (its arithmetic, in `snx_ui_models`), `CoverImage` (a cover of fixed size: cache first, texture lease,
WP-10), `fitLines()` (`TextFit.hpp`: text cut to N lines with "…"), `askText()` (WP-24: the keyboard for one line of text, answered on the main thread; a test build takes the text from the `text` command instead, since the Switch keyboard can't be typed into).

**Threading:** main thread only. Results arrive through `brls::sync` (the client's and loader's `deliver`).

**Borealis traps** (found the hard way): touches only reach focusable views (`hitTest`), so a button meant for touch must be
focusable; a `Label` under the focused view scrolls forever when its text is a hair wider than its box (a running animation:
the app is never `idle`), so turn `setAutoAnimate(false)` on for labels that sit under a focusable full-screen view.
A box skips drawing a leaf child that lies outside any parent's `getCullingBounds` (WP-22: the turned reader widens its own).
A TabFrame registers its own B ("Back") on each tab's root view after creating it, which replaces a B the tab registered
there: register a tab's own B on views inside it (WP-24: `LibraryTab`'s bar and body) and return false to pass B on.
On the Switch the hint bar shares the bottom line with the clock, battery and Wi-Fi: when the hints don't fit, borealis
cuts every label ("Refr…"), and how much moves with the clock. Keep hint texts short (WP-24: "More", "Clear").
borealis reads the mouse button once a frame, so a desktop tap shorter than a stalled frame (a config save) is lost.

**Tests:** models in `tests/unit/test_ui.cpp`, `test_ui_library.cpp`, `test_ui_manga.cpp`, `test_ui_reader.cpp` and `test_ui_strip.cpp`, `test_ui_updates.cpp` (suite
`ui`; the reader's with a real `ImageLoader`, `tests/unit/MainQueue.hpp` plays the main thread); screens by E2E scenarios
(`smoke`, `setup-first-run`, `auth-matrix`, `low-memory`, `library-browse`, `manga-details`, `reader-paged`,
`reader-rtl-spread`, `resume-progress`, `broken-images`, WP-12: `offline-errors`, `soak`, `touch-pass`, WP-22:
`reader-portrait`, WP-13: `reader-zoom`, `reader-spread-split`, WP-13b: `reader-webtoon`, WP-15: `updates-history`). Touch coordinates for the
scenarios: `tests/e2e/lib/screens.ts`.

**Status:** WP-24. Browse is a placeholder tab until WP-14.
