# WP-04 core: models, JSON, GraphQL documents, contract tests

Model: **Opus** · Size: M · Needs: WP-02 · Owner: –

## Goal
All Suwayomi data types the MVP needs, parsed robustly from JSON, plus the GraphQL documents themselves, validated
against the real schema in a contract test.

## Read first
`docs/ARCHITECTURE.md` (core), `docs/SUWAYOMI-API.md`, `tests/fixtures/schema.graphql` (search it, don't read it all), `tests/fixtures/responses/`.

## Deliverables
- [ ] `src/core/models/*.hpp`: `ServerInfo`, `Category`, `Manga`, `Chapter`, `PageList`, `Source` (stub), `ReadProgress`
- [ ] `fromJson` for each. Tolerant of missing/null/extra fields. Errors are values, never crashes
- [ ] `src/core/graphql/*.graphql` for MVP operations: aboutServer, categories, libraryByCategory (paginated), manga, chapters,
      fetchChapterPages, updateChapter (progress), login, refreshToken
- [ ] Build step embeds `.graphql` files as strings (CMake `file(READ)` → generated header)
- [ ] `GqlRequest` / `GqlResponse` (data, errors[], partial data)
- [ ] `AppConfig` load/save with `schemaVersion` + migration hook
- [ ] `RuntimeProfile` + `makeRuntimeProfile(mode, totalMem, usedMem)` (D12 amended by D21, ARCHITECTURE "Launch modes"), seeded with
      WP-00's measured numbers. Only `Full` is built. Keep the `mode` argument so WP-21 can add `Lite` later; for applet mode the function still
      returns something sane (the app's guard exits before it's used). Plus safe mode (memory budgets halved, prefetch off, SAFETY.md S1).
      Table tests: application mode at the measured memory, desktop with `--mem-limit`, safe mode, absurd inputs (used > total, 0)
- [ ] Unit tests: every model against recorded responses, plus nasty cases (nulls, unicode, huge ints, wrong types, empty lists, `errors[]`)
- [ ] Contract test: `tests/contract/` (Node `graphql` package) validates every `.graphql` file against `schema.graphql`. Wired into `tools/build.sh tests`
- [ ] Safety (SAFETY.md S6): JSON nesting-depth + size limits in the parser wrapper. Fuzz targets (libFuzzer, ASan/UBSan) for every `fromJson`,
      `GqlResponse` and `AppConfig` load, plus `tools/build.sh fuzz [seconds]`. Seed corpus = recorded responses
- [ ] `src/core/README.md` updated

## Acceptance
Unit + contract tests green. `core` coverage ≥ 80%. `core` has zero includes of curl/borealis/libnx (add a CMake check).

## Sonnet-safe chunks
- Extra table-driven parser cases once Opus has written the first test of each model.
