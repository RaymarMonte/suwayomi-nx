# WP-05 net: HttpClient, worker pool, cancellation

Model: **Opus** · Size: M · Needs: WP-01 (can run in parallel with WP-04, as they touch different folders) · Owner: –

## Goal
A small, boring, well-tested HTTP layer used by both `api` and `image`, with priorities and cancellation, identical on desktop and Switch.

## Read first
`docs/ARCHITECTURE.md` (net, threading). Switchfin's HTTP wrapper for ideas (Apache-2.0, credit if adapted).

## Deliverables
- [ ] `HttpClient` interface, `Request`, `Response`, `Priority`, `CancelToken`, `Future<T>` (or a callback-on-main-thread helper, pick one and record it in DECISIONS)
- [ ] `CurlHttpClient`: one thread driving a curl multi handle, `maxConcurrent` passed in (6 in `Full`, from `RuntimeProfile`),
      keep-alive, connect/total timeouts per request, max body size, gzip, redirects (limited)
- [ ] `RequestDecorator` hook (auth headers), `ResponseObserver` hook (401 handling, test-hook `net` events)
- [ ] Safety: per-request max body size (JSON 8 MB, image 32 MB defaults) enforced *while streaming* (abort early, don't buffer then check).
      Explicit worker stack size. Clean shutdown + reconnect after sleep/resume (stale sockets → fresh handles, no crash)
- [ ] Retry policy (idempotent GETs only, backoff from `core`)
- [ ] `FakeHttpClient` for other modules' unit tests
- [ ] Host tests with a local cpp-httplib server: success, 404, 401, timeout, slow body + cancel, many parallel requests, priority ordering, shutdown with jobs in flight
- [ ] Switch: confirm which socket config is active. borealis's `userAppInit` uses 12 BSD sessions in application mode (2 in applet mode,
      which the app doesn't run in, D21). Keep it, or override it in `platform/switch` and record that in DECISIONS. Check with the Hello app
      fetching `aboutServer` from the test server in application mode, plus a burst of 20 parallel requests (must queue, not fail)
- [ ] `src/net/README.md`

## Acceptance
All host tests green with ThreadSanitizer on (`-fsanitize=thread`), with no data races. On the Switch the Hello app shows the test server's version.

## Sonnet-safe chunks
- Additional status-code/edge test cases once the test server fixture exists.
