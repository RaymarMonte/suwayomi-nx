# WP-06 api: SuwayomiClient + auth modes

Model: **Opus** · Size: M · Needs: WP-04, WP-05 · Owner: –

## Goal
A typed client for every MVP operation, handling all supported auth modes (including JWT refresh), with integration tests
against the live test server in every auth profile.

## Read first
`docs/SUWAYOMI-API.md`, `src/core/README.md` + model headers, `src/net/README.md` + `HttpClient.hpp`.

## Deliverables
- [x] `SuwayomiClient` methods: `serverInfo`, `categories`, `library(categoryId, page)`, `manga`, `chapters`, `pages(chapterId)`,
      `setProgress(chapterId, page, isRead)`, `login`, `logout`
- [x] Absolute-URL helper for page/thumbnail paths (base URL with sub-path, trailing slashes, `?format=`)
- [x] Auth: `none`, `basic_auth`, `ui_login` (store access+refresh, refresh on 401/expiry once, single-flight so 5 parallel 401s cause 1 refresh,
      then retry. On refresh failure → `ApiError::Auth`). `simple_login` documented as unsupported for now
- [x] Image requests carry the same auth (Basic/Bearer header)
- [x] `ApiError` mapping (Offline, Timeout, Auth, Permission, Server, Parse) with user-facing messages
- [x] Server version check: warn below the minimum from D9
- [x] Unit tests with `FakeHttpClient` + recorded responses (incl. 401→refresh→retry, refresh fails, GraphQL `errors[]`)
- [x] Integration tests (`tests/integration/`) against the test server in `none`, `basic`, `ui_login` (20 s expiry → wait → still works)
- [x] Progress round-trip test: set page 5 → read back 5. Clamp beyond pageCount
- [x] `src/api/README.md`

## Acceptance
Unit + integration green in all three auth profiles. TSan clean on the refresh single-flight test.

## Sonnet-safe chunks
- Error message strings + their mapping table tests.
