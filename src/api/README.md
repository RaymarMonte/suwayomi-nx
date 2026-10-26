# api

**Purpose:** `SuwayomiClient`: typed, asynchronous calls to Suwayomi-Server's GraphQL API that return `core` models, the login
(none / basic / ui_login with token renewal), and full URLs for pages and thumbnails (ARCHITECTURE.md "api", SUWAYOMI-API.md, D38).

**Public headers** (include as `"api/<Name>.hpp"`):
- `SuwayomiClient.hpp`: `SuwayomiClient(http, serverSettings, clientOptionsFrom(profile))` with `serverInfo`, `categories`,
  `library(categoryId, page)`, `manga`, `chapters(mangaId, page)`, `pages(chapterId, PageFormat)`, `setProgress(chapterId, page,
  isRead)`, `login`, `logout`, `configure`, `settings`, `absoluteUrl`. Each call takes a `net::CancelToken` (+ optional `Priority`)
  and returns `ApiFuture<T>` = `net::Future<ApiResult<T>>`. Also `basicAuthHeader`, `serverWarning` (D9 minimum version).
- `ApiError.hpp`: `ApiError{kind, message, detail}`, kinds Offline|Timeout|Auth|Permission|Server|Parse|Cancelled, `userMessage(kind)`,
  `ApiResult<T>` (= `core::Result<T, ApiError>`), and the mapping from core/net errors and HTTP statuses.
- `ServerUrl.hpp`: `normalizeServerUrl` (what the user typed), `graphqlUrl`, `absoluteUrl` (sub-path, slashes, `?format=` kept), `isServerUrl`.
- `Api.hpp`: `userAgent()`.

**How to use it:** the app makes one client per `HttpClient` and keeps it (it becomes that client's request decorator). Set
`ClientOptions::deliver` to `brls::sync`, so `then()` runs on the UI thread, and `onSettingsChanged` to save the new tokens in
config.json. A call cancelled before its result is delivered ends `Cancelled`: check that first and return.

**Login:** none: nothing. basic: `Authorization: Basic` on every request. ui_login: `Bearer <access token>` on calls; `login`
and `refreshToken` go out without it. A refused token (GraphQL "Unauthorized" or HTTP 401) is renewed once: `refreshToken`, and when
the server refuses that, `login` with the saved username + password. Calls refused at the same time share one renewal (single
flight); a refused renewal is remembered until the login changes, so the server isn't asked again and again; no answer (Offline)
isn't remembered. Images and thumbnails get the same header from the decorator; an image answered 401 starts a background
renewal, so the image module only has to try it once more. `simple_login` (cookie) isn't supported (D38).

**Depends on:** `core`, `net`. No borealis: results reach the UI through `deliver`.

**Threading:** no threads of its own. Calls may start on any thread; answers are handled on the network thread, then delivered.
Shared login state is behind one mutex; calls keep it alive (`shared_ptr`), so they finish safely after the client is destroyed.
The `HttpClient` must outlive the client.

**Tests:** `tests/unit/test_api.cpp` (errors, URLs, helpers), `test_api_client.cpp` (`FakeHttpClient` + recorded answers, every auth
mode, renewal cases), `test_api_curl.cpp` (single flight on real threads, for `tools/build.sh tsan`), doctest suite `api`.
Integration against the test server in all three auth modes: `tools/build.sh integration` (`tests/integration/`, ~1 min).

**Status:** WP-06.
