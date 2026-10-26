#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Result.hpp"
#include "net/CancelToken.hpp"
#include "net/Future.hpp"

// The HTTP layer every other module uses (ARCHITECTURE.md "net", WP-05). CurlHttpClient is the real one, FakeHttpClient the
// stand-in for unit tests. Both behave the same way from the outside.
namespace snx::net {

// Who is waiting, most urgent first. Queued requests start in this order; within one priority, first come first served.
enum class Priority {
    Interactive = 0,  // something the user is looking at right now
    Reader = 1,       // the reader page on screen and its neighbours
    Prefetch = 2,     // pages and covers the user will probably need soon
    Background = 3,   // anything else
};
inline constexpr int kPriorityCount = 4;
const char* priorityName(Priority priority);

enum class Method { Get, Post };

using Header = std::pair<std::string, std::string>;

struct Request {
    Method method = Method::Get;
    std::string url;
    std::vector<Header> headers;  // e.g. {"Content-Type", "application/json"}
    std::string body;             // POST only
    // Safe to send twice, so the retry policy may repeat it. GETs always are. Set it on a POST only when repeating can't change
    // anything twice: a GraphQL query, never a mutation (api, WP-06).
    bool idempotent = false;
    // Limits for this one request. 0 = the client's default, which comes from the RuntimeProfile (json.maxBytes for the body,
    // httpTimeoutMs, httpConnectTimeoutMs). The image module passes maxImageBytes and httpImageTimeoutMs here.
    std::uint64_t maxBodyBytes = 0;
    int timeoutMs = 0;
    int connectTimeoutMs = 0;
};

struct Response {
    int status = 0;               // the HTTP status code
    std::vector<Header> headers;  // names in lower case; after redirects, only the last answer's headers
    std::string body;             // already un-gzipped
    int attempts = 1;             // 2 or more when the retry policy tried again

    // The first header with this name (any case), or "" if there is none.
    std::string header(std::string_view name) const;
};

// Any HTTP answer (404 and 401 too) is a Response. No answer at all is an Error with kind Network, Timeout, Cancelled or
// TooLarge (core/Result.hpp).
using HttpResult = core::Result<Response>;

// Hooks. Both run on the network thread: keep them short and never touch UI from them.
// - RequestDecorator: changes a request just before each attempt starts, e.g. adds the Authorization header (api, WP-06).
//   It runs again for a retry, so a refreshed login is used.
// - ResponseObserver: sees how each request ended (after retries, the request as decorated) just before its future
//   completes, e.g. notices a 401 or writes a test "net" event. Requests refused at submit (after shutdown) skip it.
using RequestDecorator = std::function<void(Request&)>;
using ResponseObserver = std::function<void(const Request&, const HttpResult&)>;

class HttpClient {
  public:
    virtual ~HttpClient() = default;

    // Queues a request. The future always completes exactly once: with the answer or with an error (Cancelled if the token
    // fires first or the client shuts down). Any thread may call it.
    virtual Future<HttpResult> submit(Request request, Priority priority, CancelToken cancel) = 0;

    // After the console wakes from sleep, or the network comes back: forgets every kept-alive connection so nothing uses a
    // socket that went stale. Running requests fail as dropped connections, which GETs retry on fresh connections.
    virtual void resetConnections() = 0;

    // Ends every queued and running request with Cancelled, then stops. Later submits fail at once. The destructor calls it.
    // Never call it from a then() callback or a hook (they run on the network thread, which shutdown waits for).
    virtual void shutdown() = 0;

    // Requests submitted and not finished yet: queued, running or waiting for a retry (WP-08: the app is "idle" only when this
    // is 0). It drops after the request's future has completed, so its then() callback has run by then. Any thread.
    virtual std::size_t inFlight() const { return 0; }

    // Best set once, before the first submit (any thread may call them, though).
    void setRequestDecorator(RequestDecorator decorator);
    void addResponseObserver(ResponseObserver observer);

  protected:
    void decorate(Request& request) const;
    void notifyObservers(const Request& request, const HttpResult& result) const;

  private:
    mutable std::mutex hooksMutex_;
    RequestDecorator decorator_;
    std::vector<ResponseObserver> observers_;
};

}  // namespace snx::net
