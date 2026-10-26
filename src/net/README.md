# net

**Purpose:** HTTP for the whole app (ARCHITECTURE.md "net", D5, D37): an `HttpClient` interface, a libcurl implementation with one
network thread driving a curl multi handle, request priorities, cancellation, retries, and a fake for other modules' unit tests.

**Public headers** (include as `"net/<Name>.hpp"`):
- `HttpClient.hpp`: `Request` (method, url, headers, body, `idempotent`, per-request `maxBodyBytes`/`timeoutMs`/`connectTimeoutMs`),
  `Response` (status, lower-case headers, body, attempts), `HttpResult = core::Result<Response>`, `Priority`
  (Interactive > Reader > Prefetch > Background), the `HttpClient` interface (`submit`, `resetConnections`, `shutdown`) and its hooks
  `setRequestDecorator` (auth headers, runs before every attempt) and `addResponseObserver` (401 handling, test events).
- `CancelToken.hpp`: a shared stop flag. Copies share it; `cancel()` drops queued requests and stops running ones within ~50 ms.
- `Future.hpp`: `Future<T>` / `Promise<T>`. `then(fn)` runs fn once on the completing thread (the network thread); `wait()` is for tests.
- `CurlHttpClient.hpp`: the real client. Build it with `CurlHttpClient(curlOptionsFrom(profile))`: every limit comes from the
  `RuntimeProfile` (`httpConcurrency`, `json.maxBytes`, `httpConnectTimeoutMs`, `httpTimeoutMs`, `httpMaxRedirects`,
  `httpMaxHeaderBytes`, `httpRetry`, `netThreadStackBytes`).
- `FakeHttpClient.hpp`: for unit tests. A handler answers each request (default 404), requests are recorded, `hold(true)` keeps them
  waiting until `releaseNext()`/`releaseAll()` (most urgent first).

**What a caller gets:** the future completes exactly once. Any HTTP answer (404, 401, 500 too) is a `Response`; no answer is an
`Error` with kind `Network` (refused, reset, too many redirects, bad URL or scheme), `Timeout`, `Cancelled` (token or shutdown) or
`TooLarge` (body over its limit, stopped while streaming; or headers over `httpMaxHeaderBytes`). Nothing throws.

**Behaviour of `CurlHttpClient`:**
- At most `maxConcurrent` transfers (6 in the full profile); curl's connection pool is capped at the same number, so sockets stay few.
  Kept-alive connections are reused. gzip/deflate answers are un-gzipped; the body limit applies to the un-gzipped size.
- Redirects: followed, at most `httpMaxRedirects`, http/https only. Header values with line breaks are dropped (no header injection).
- Retries (`core::RetryPolicy`): GETs, and POSTs with `idempotent = true`, after a transient failure (refused/reset connection, empty
  reply, HTTP 408/429/502/503/504): 2 retries, 250 ms then 500 ms apart, first in line when due. Timeouts are not retried.
- `resetConnections()` (call it after sleep/resume): running transfers are dropped and retried on a fresh connection pool.
- `shutdown()` (the destructor calls it): every queued and running request ends `Cancelled`; later submits fail at once.
- `inFlight()` (WP-08): requests submitted and not finished (queued, running, waiting to retry). It drops after the future completed, so callbacks have run; the app's idle check uses it.
  Never call it from a `then()` callback or a hook: they run on the network thread.
- DNS: the PC's curl resolves in a helper thread; the Switch's curl 7.69 resolves on the network thread itself, so a slow name lookup
  stalls all transfers. Use the server's IP address (the setup screen says so, WP-08).
- HTTPS: certificate checks are on. Whether the Switch's curl (TLS through libnx) finds a CA store is not tested yet (no HTTPS
  test server); plain HTTP on the LAN is the normal Suwayomi setup.

**Depends on:** `core`, libcurl (PC: `libcurl4-openssl-dev`; Switch: devkitPro `switch-curl` 7.69 + `switch-zlib`), pthreads.

**Threading:** one network thread per `CurlHttpClient` (pthread with an explicit stack size). `submit`, `cancel`, `resetConnections`
and `shutdown` are safe from any thread. Hooks and `then()` callbacks run on the network thread: keep them short, never touch UI
there; the api layer hands results to the UI with `brls::sync`.

**Tests:** `tests/unit/test_net.cpp` (token, future, fake) and `tests/unit/test_net_curl.cpp` (the curl client against a real
cpp-httplib server on 127.0.0.1: success, 404, 401, POST, timeout, refused, redirects, body limits, cancel, 40 parallel requests,
priority order, retries, reset, shutdown in flight), doctest suite `net`. `tools/build.sh tsan` runs them under ThreadSanitizer.

**Status:** WP-05. WP-08 added `inFlight()`.
