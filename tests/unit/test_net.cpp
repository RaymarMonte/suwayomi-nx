#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include "net/CancelToken.hpp"
#include "net/FakeHttpClient.hpp"
#include "net/Future.hpp"
#include "net/HttpClient.hpp"

// The parts of net that need no network: CancelToken, Future, Response, FakeHttpClient. The curl client is in test_net_curl.cpp.
using namespace snx::net;
using snx::core::ErrorKind;

namespace {
Request get(std::string url) {
    Request r;
    r.url = std::move(url);
    return r;
}
}  // namespace

TEST_SUITE("net") {

TEST_CASE("CancelToken: copies share one flag") {
    CancelToken token;
    CancelToken copy = token;
    CHECK_FALSE(copy.cancelled());
    token.cancel();
    CHECK(copy.cancelled());
    CHECK_FALSE(CancelToken{}.cancelled());
}

TEST_CASE("Future: then() before and after the result, wait across threads") {
    Promise<int> promise;
    Future<int> future = promise.future();
    CHECK(future.valid());
    CHECK_FALSE(future.ready());
    CHECK_FALSE(Future<int>{}.valid());

    int early = 0;
    future.then([&](const int& v) { early = v; });
    std::thread worker([promise]() mutable { promise.set(42); });
    CHECK(future.waitFor(std::chrono::seconds(5)));
    worker.join();
    CHECK(future.get() == 42);
    CHECK(early == 42);

    int late = 0;
    future.then([&](const int& v) { late = v; });  // already there: runs at once, here
    CHECK(late == 42);

    promise.set(7);  // only the first set counts
    CHECK(future.get() == 42);
}

TEST_CASE("Response::header ignores case") {
    Response r;
    r.headers = {{"content-type", "application/json"}, {"x-a", "1"}, {"x-a", "2"}};
    CHECK(r.header("Content-Type") == "application/json");
    CHECK(r.header("X-A") == "1");
    CHECK(r.header("missing").empty());
    CHECK(std::string(priorityName(Priority::Prefetch)) == "prefetch");
}

TEST_CASE("FakeHttpClient: handler, 404 default, decorator, observers") {
    FakeHttpClient fake;
    auto missing = fake.submit(get("http://x/none"), Priority::Interactive, {});
    REQUIRE(missing.ready());
    CHECK(missing.get().value().status == 404);

    fake.setHandler([](const Request& r) { return FakeHttpClient::answer(200, "hi " + r.url); });
    fake.setRequestDecorator([](Request& r) { r.headers.push_back({"Authorization", "Bearer t"}); });
    int seen = 0;
    fake.addResponseObserver([&](const Request&, const HttpResult& result) { seen += result.ok() ? 1 : 0; });
    auto ok = fake.submit(get("http://x/a"), Priority::Interactive, {});
    CHECK(ok.get().value().body == "hi http://x/a");
    CHECK(seen == 1);
    REQUIRE(fake.requests().size() == 2);
    CHECK(fake.requests()[1].headers.at(0).second == "Bearer t");
}

TEST_CASE("FakeHttpClient: hold releases most urgent first, cancelled ones end Cancelled, shutdown cancels the rest") {
    FakeHttpClient fake;
    fake.setHandler([](const Request& r) { return FakeHttpClient::answer(200, r.url); });
    fake.hold(true);
    std::vector<std::string> order;
    auto track = [&](Future<HttpResult> f) { f.then([&](const HttpResult& r) { order.push_back(r.ok() ? r.value().body : "x"); }); };
    CancelToken gone;
    track(fake.submit(get("bg"), Priority::Background, {}));
    track(fake.submit(get("pre"), Priority::Prefetch, {}));
    track(fake.submit(get("ui1"), Priority::Interactive, {}));
    track(fake.submit(get("ui2"), Priority::Interactive, {}));
    auto cancelled = fake.submit(get("reader"), Priority::Reader, gone);
    auto last = fake.submit(get("last"), Priority::Background, {});
    CHECK(fake.waitingCount() == 6);
    gone.cancel();
    for (int i = 0; i < 5; ++i) {  // ui1, ui2, reader (cancelled), pre, bg
        CHECK(fake.releaseNext());
    }
    CHECK(fake.waitingCount() == 1);
    CHECK(order == std::vector<std::string>{"ui1", "ui2", "pre", "bg"});
    CHECK(cancelled.get().error().kind == ErrorKind::Cancelled);
    fake.shutdown();
    CHECK(last.get().error().kind == ErrorKind::Cancelled);
    CHECK(fake.submit(get("after"), Priority::Interactive, {}).get().error().kind == ErrorKind::Cancelled);
    fake.resetConnections();
    CHECK(fake.resetCount() == 1);
}

}  // TEST_SUITE("net")
