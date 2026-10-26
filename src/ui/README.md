# ui

**Purpose:** the borealis screens (ARCHITECTURE.md "ui", D40). Touch first (D16): everything works with a tap, and with a controller.

**The screen pattern** (copy it for every new screen):
- `src/ui/<screen>/<Name>Activity.{hpp,cpp}`: the view. Layout in `resources/xml/activity/<name>.xml`, views bound with `BRLS_BIND`,
  texts from `resources/i18n/en-US/<screen>.json`. It takes `app::AppContext&` (everything shared: client, images, config, profile).
- `src/ui/<screen>/<Name>Model.{hpp,cpp}`: the state and rules, **no borealis**, so `tests/unit/test_ui.cpp` (suite `ui`) tests it.
  Part of the library `snx_ui_models` (`src/ui/CMakeLists.txt`); the views are compiled into the app (`src/app/CMakeLists.txt`).
- Every network call takes the screen's `net::CancelToken`; the destructor cancels it; a callback checks `token.cancelled()` first.
- A screen reports itself with `app::testhooks::screen("<Name>")` and shows errors with `StatusView::showError` (writes `error`).
- Touch targets ≥ 72 px: cells `minHeight="72"`, buttons `height="72"`.

**Screens:** `main/MainActivity` (tab frame, always the first screen, the connection check; `SettingsTab`, `CrashLogsActivity`),
`setup/SetupActivity` (server address, login mode, test, warnings), `login/LoginActivity` (ui_login), `crash/CrashActivity`
(the crash screen, S1), `diagnostics/DiagnosticsActivity` (test builds only: WP-05's server check + request burst, WP-07's image benchmark).

**Components** (`components/`): `StatusView` (loading / error + Retry / message), `TextFieldCell` (XML `<TextFieldCell>`: a tap
opens the keyboard; secret fields show dots), `errorText()` (says "no network" when the console has none), `checkServer()`
(version + categories when a login is configured, D40).

**Threading:** main thread only. Results arrive through `brls::sync` (the client's and loader's `deliver`).

**Tests:** models in `tests/unit/test_ui.cpp`; screens by E2E scenarios (`smoke`, `setup-first-run`, `auth-matrix`, `low-memory`).
Touch coordinates for the scenarios: `tests/e2e/lib/screens.ts`.

**Status:** WP-08. Library, Updates, History and Browse are placeholder tabs until their WPs.
