# net

**Purpose:** HTTP for the whole app: an `HttpClient` interface, a libcurl implementation with one network thread driving a curl
multi handle (D5), request priorities and cancellation (ARCHITECTURE.md "net").

**Public headers** (include as `"net/<Name>.hpp"`):
- `Net.hpp`: `moduleName()`, a placeholder so the module exists and links.

**Depends on:** `core`. libcurl from WP-05 on (devkitPro `switch-curl` on the Switch, system curl on the PC).

**Threading:** from WP-05: one network thread. Callers get futures; results reach the UI only through `brls::sync`.
Every request takes a cancellation token.

**Tests:** `tests/unit/test_net.cpp` (doctest suite `net`). Later a fake `HttpClient` for unit tests and a local HTTP server
for curl-level tests. Coverage target ≥ 60% lines.

**Status:** placeholder from WP-01. The real module is WP-05.
