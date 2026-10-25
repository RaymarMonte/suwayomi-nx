# api

**Purpose:** `SuwayomiClient`: typed, asynchronous calls to Suwayomi-Server's GraphQL API that return `core` models, plus auth
(none / basic / JWT login with refresh) and absolute URLs for pages and thumbnails (ARCHITECTURE.md "api", SUWAYOMI-API.md).

**Public headers** (include as `"api/<Name>.hpp"`):
- `Api.hpp`: `userAgent()` ("suwayomi-nx/<version>"), a placeholder from WP-01.

**Depends on:** `core`, `net`.

**Threading:** from WP-06: no threads of its own. Calls go through `net`'s network thread and return futures. Each call takes a
cancellation token.

**Tests:** `tests/unit/test_api.cpp` (doctest suite `api`) with a fake `HttpClient`. Integration tests against the Suwayomi test
server come with WP-02/WP-06. Coverage target ≥ 60% lines.

**Status:** placeholder from WP-01. The real module is WP-06.
