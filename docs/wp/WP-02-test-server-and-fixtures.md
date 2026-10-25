# WP-02 Suwayomi test server + fixture manga + schema dump

Model: **Opus** · Size: M · Needs: WP-01 · Owner: accept the firewall prompt for Java on Private networks

## Goal
A one-command, throwaway Suwayomi test server on Windows (port 4568) seeded with synthetic fixture manga in Local Source.
Plus a checked-in GraphQL schema and recorded responses, so everything else can be tested offline and deterministically.

## Read first
`docs/SUWAYOMI-API.md`, `docs/TESTING.md` (Fixture manga section), DECISIONS D9, D11.

## Deliverables
- [ ] `tools/test-server/` with `start|stop|reset|status` (PowerShell for Windows Java, callable from WSL). Downloads a **pinned**
      Suwayomi jar version to a cache dir, uses a separate `rootDir` (never the owner's real data), port 4568, tray/browser/KCEF off
- [ ] Auth profiles: `--auth none|basic|ui_login` (ui_login with short `jwtTokenExpiry` like 20s for refresh tests)
- [ ] `tools/gen-fixtures/` (Node + sharp): generates the fixture set from TESTING.md, including the 4×4 colour-code block,
      into `<rootDir>/local/`. Deterministic output (same bytes every run)
- [ ] A decoder for the colour block (`tests/e2e/lib/pagecode.ts`) + unit test using a generated page
- [ ] Seed script: add fixture manga to the library, create 2 categories, assign manga (via GraphQL)
- [ ] `tools/dump-schema`: introspection → `tests/fixtures/schema.graphql` + `tests/fixtures/server-version.txt`
- [ ] `tools/record-fixtures`: runs a set of queries and saves JSON responses to `tests/fixtures/responses/*.json` (for unit tests)
- [ ] Confirm or fix every ❔ in SUWAYOMI-API.md from the real schema
- [ ] Reachability check from WSL and from another LAN device (e.g. a phone browser at `http://<pc-ip>:4568`). Document the Windows IP
      and firewall rule. The check from the Switch itself happens in WP-05, when the Hello app fetches `aboutServer`
- [ ] CLAUDE.md commands updated

## Acceptance
`reset && start` gives a server with the fixture library in < 60 s. Schema and responses are committed. Running it twice
produces identical fixture files. The ❔ list in SUWAYOMI-API.md is resolved.

## Sonnet-safe chunks
- Adding more fixture series once the generator exists.
- The record-fixtures query list, given the schema.
