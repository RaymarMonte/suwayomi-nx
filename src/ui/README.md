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
`CrashLogsActivity`), `library/LibraryTab` (categories, sort, refresh, the cover grid), `manga/MangaActivity` (details + chapter list, WP-10,
D43: `MangaModel` holds the list, sort, filter, Continue and the actions), `reader/ReaderActivity` (a stand-in until WP-11), `setup/SetupActivity` (server address, login mode, test, warnings), `login/LoginActivity` (ui_login),
`crash/CrashActivity` (the crash screen, S1), `diagnostics/DiagnosticsActivity` (test builds only: WP-05's server check + request
burst, WP-07's image benchmark).

**Components** (`components/`): `StatusView` (loading / error + Retry / message; never takes the focus while hidden),
`TextFieldCell` (XML `<TextFieldCell>`: a tap opens the keyboard; secret fields show dots), `errorText()` (says "no network"
when the console has none), `checkServer()` (version + categories when a login is configured, D40), `CoverGrid` + `GridCell` +
`GridSource` (the recycling grid, D42; one column makes it a list, as on the manga screen; `rebind()` redraws rows in place)
and `GridLayout` (its arithmetic, in `snx_ui_models`), `CoverImage` (a cover of fixed size: cache first, texture lease,
WP-10), `fitLines()` (`TextFit.hpp`: text cut to N lines with "…").

**Threading:** main thread only. Results arrive through `brls::sync` (the client's and loader's `deliver`).

**Tests:** models in `tests/unit/test_ui.cpp`, `test_ui_library.cpp` and `test_ui_manga.cpp` (suite `ui`); screens by E2E
scenarios (`smoke`, `setup-first-run`, `auth-matrix`, `low-memory`, `library-browse`, `manga-details`). Touch coordinates for the scenarios: `tests/e2e/lib/screens.ts`.

**Status:** WP-10. Updates, History and Browse are placeholder tabs until their WPs; the reader comes with WP-11 (it then
calls `MangaModel::reloadChapters()` on the way back, so the list shows the new progress).
