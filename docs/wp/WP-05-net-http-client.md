# WP-05 net: HttpClient, worker pool, cancellation

Model: **Opus** · Size: M · Needs: WP-01 (can run in parallel with WP-04, as they touch different folders) · Owner: –

## Goal
A small, boring, well-tested HTTP layer used by both `api` and `image`, with priorities and cancellation, identical on desktop and Switch.

## Read first
`docs/ARCHITECTURE.md` (net, threading). Switchfin's HTTP wrapper for ideas (Apache-2.0, credit if adapted).

## Deliverables
- [x] `HttpClient` interface, `Request`, `Response`, `Priority`, `CancelToken`, `Future<T>` (or a callback-on-main-thread helper, pick one and record it in DECISIONS)
- [x] `CurlHttpClient`: one thread driving a curl multi handle, `maxConcurrent` passed in (6 in `Full`, from `RuntimeProfile`),
      keep-alive, connect/total timeouts per request, max body size, gzip, redirects (limited)
- [x] `RequestDecorator` hook (auth headers), `ResponseObserver` hook (401 handling, test-hook `net` events)
- [x] Safety: per-request max body size (JSON 8 MB, image 32 MB defaults) enforced *while streaming* (abort early, don't buffer then check).
      Explicit worker stack size. Clean shutdown + reconnect after sleep/resume (stale sockets → fresh handles, no crash)
- [x] Retry policy (idempotent GETs only, backoff from `core`)
- [x] `FakeHttpClient` for other modules' unit tests
- [x] Host tests with a local cpp-httplib server: success, 404, 401, timeout, slow body + cancel, many parallel requests, priority ordering, shutdown with jobs in flight
- [x] Switch: confirm which socket config is active. borealis's `userAppInit` uses 12 BSD sessions in application mode (2 in applet mode,
      which the app doesn't run in, D21). Keep it, or override it in `platform/switch` and record that in DECISIONS. Check with the Hello app
      fetching `aboutServer` from the test server in application mode, plus a burst of 20 parallel requests (must queue, not fail)
- [x] `src/net/README.md`

## Acceptance
All host tests green with ThreadSanitizer on (`-fsanitize=thread`), with no data races. On the Switch the Hello app shows the test server's version.

## Sonnet-safe chunks
- Additional status-code/edge test cases once the test server fixture exists.

## Done (2026-10-26)
All items done (D37). Host tests: `tests/unit/test_net.cpp`, `test_net_curl.cpp`, green under ThreadSanitizer (`tools/build.sh tsan`,
also in CI); net line coverage 95 %. Switch: borealis's socket setup kept (12 BSD sessions in application mode); the Hello app
shows the test server's version and a burst of 20 requests all answer (`tools/e2e.sh switch net-hello`). Future that `api` (WP-06)
wraps with `brls::sync`; POSTs retry only when marked `idempotent` (GraphQL queries).
