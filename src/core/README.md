# core

**Purpose:** everything that is pure C++: models, JSON mapping, GraphQL documents, reader math, cache and retry policies,
`RuntimeProfile`, config (ARCHITECTURE.md "core"). It must never include curl, borealis, libnx or platform headers, so it is
100% testable on the PC. ctest `unit.core-includes` (`CheckIncludes.cmake`) enforces that with an allowlist.

**Public headers** (include as `"core/<Name>.hpp"`):
- `Result.hpp` (WP-04): `Result<T>` = a value or an `Error{kind, message}`; kinds Parse|Server|Unauthorized, and Network|Timeout|Cancelled|TooLarge for net (WP-05). Parsers never throw.
  `Result<T, E>` takes another error type: api's `ApiResult<T>` = `Result<T, ApiError>` (WP-06).
- `Json.hpp` (WP-04): `parseJson(text, JsonLimits)` with S6 size (8 MiB) and depth (64) limits, `dumpJson` (never throws), and
  `json::string/int32/int64/number/boolean/strings/object/array` field readers (missing, null, wrong type → nothing).
- `models/*.hpp` (WP-04): plain structs `ServerInfo` (+ D9 version check), `Category`, `Source`, `Manga`, `Chapter`, `PageList`,
  `ReadProgress`, `LoginTokens`, `NodeList<T>`. `models/FromJson.hpp`: `fromJson<T>(node)` and `nodeListFromJson<T>(list)`.
  Tolerant: a model fails only without an object or a valid id; broken list entries are skipped and counted.
- `graphql/Gql.hpp` (WP-04): `GqlRequest` (`body()`), `GqlResponse` (data, cleaned `errors[]`), `parseGqlResponse()`.
- `graphql/Operations.hpp` (WP-04): the MVP operations, each a request builder plus an answer reader (`gql::manga(id)` →
  `gql::readManga(response)`). Documents: `graphql/*.graphql`, embedded at build time (`EmbedGraphql.cmake`), list in `Documents.hpp`.
- `AppConfig.hpp` (WP-04): settings ↔ config.json text, `schemaVersion` + migration steps; loading never fails (defaults + `problems`).
- `RuntimeProfile.hpp` (WP-04): `makeRuntimeProfile(mode, totalMem, usedMem, safeMode)` → every budget and limit (D12/D21).
- `RetryPolicy.hpp` (WP-05): `retryDelayMs` (doubling, capped), `shouldRetry` (idempotent + transient + retries left), `isTransientHttpStatus`.
- `Version.hpp`, `LaunchOptions.hpp`, `RuntimeInfo.hpp`, `TestEvents.hpp` (WP-01/03): version, flags, launch mode, test events.
- `SafePath.hpp`, `CrashRecord.hpp`, `TestConfig.hpp` (WP-03b): write-jail path rules, crash record format, test config.
- `LogBuffer.hpp`, `SaveScheduler.hpp` (WP-08): the log file's line format, buffering and rotation; when to save config.json (debounce). WP-08 also added test config keys (server login, `showSetup`, `diagnostics`), test events (`launch_refused`, `mem`, screen params) and profile fields (`logFileBytes`, `logFlushMs`, `configSaveDelayMs`).

**Depends on:** the C++17 standard library and nlohmann/json 3.12.0 (`third_party/`, include `<nlohmann/json.hpp>`).

**Threading:** no threads of its own. Functions are pure or work on caller-owned data, so they can be called from any thread.

**Tests:** doctest suite `core` (`tests/unit/test_core*.cpp`), models against real answers in `tests/fixtures/responses/` (`app-*.json`
are our own documents' answers, `tools/record-fixtures`). Contract: `npm run contract` (`tests/contract/`). Fuzz: `tools/build.sh fuzz`
(`tests/fuzz/`). Coverage ≥ 80% lines (94% after WP-04).

**Status:** WP-04 added models, JSON, GraphQL, config and the runtime profile. WP-10: `Chapter::lastReadAt`, `ChapterPatch`,
the `UpdateChapters` and `FetchMangaAndChapters` documents, `RuntimeProfile::maxChapters`. Reader math and policies come with WP-07/WP-11.
