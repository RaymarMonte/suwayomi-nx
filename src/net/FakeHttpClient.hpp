#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include "net/HttpClient.hpp"

namespace snx::net {

// A stand-in HttpClient for other modules' unit tests: no network, no threads, fully predictable.
// - A handler decides each answer (no handler: HTTP 404). Requests are recorded as decorated, in submit order.
// - Normally a request completes inside submit() (so then() callbacks run on the test's thread).
// - hold(true): requests wait until releaseNext()/releaseAll(), most urgent first like the real client. That tests ordering,
//   cancelling and "still loading" states. A request whose token fired by then ends Cancelled.
class FakeHttpClient final : public HttpClient {
  public:
    using Handler = std::function<HttpResult(const Request&)>;

    // A ready-made answer for handlers.
    static HttpResult answer(int status, std::string body = {}, std::vector<Header> headers = {});

    void setHandler(Handler handler);
    void hold(bool on);
    bool releaseNext();         // false if nothing was waiting
    std::size_t releaseAll();   // how many were released
    std::size_t waitingCount() const;
    std::vector<Request> requests() const;
    int resetCount() const;

    Future<HttpResult> submit(Request request, Priority priority, CancelToken cancel) override;
    void resetConnections() override;
    void shutdown() override;
    std::size_t inFlight() const override { return waitingCount(); }  // held requests; others complete at once

  private:
    struct Waiting {
        Request request;
        Priority priority;
        CancelToken cancel;
        Promise<HttpResult> promise;
    };
    void complete(Waiting& item);

    mutable std::mutex mutex_;
    Handler handler_;
    bool holding_ = false;
    bool stopped_ = false;
    int resets_ = 0;
    std::vector<Request> requests_;
    std::deque<Waiting> waiting_;
};

}  // namespace snx::net
