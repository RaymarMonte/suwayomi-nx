#include <doctest/doctest.h>
#include <httplib/httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/RuntimeProfile.hpp"
#include "net/CurlHttpClient.hpp"

// CurlHttpClient against a real HTTP server on 127.0.0.1 (cpp-httplib, host only). Run under ThreadSanitizer too:
// tools/build.sh tsan.
using namespace snx::net;
using namespace std::chrono_literals;
using snx::core::ErrorKind;

namespace {

// A local server on a free port, stopped at the end of the test.
struct LocalServer {
    httplib::Server server;
    std::thread thread;
    int port = 0;

    LocalServer() {
        server.new_task_queue = [] { return new httplib::ThreadPool(32); };
    }
    void start() {
        port = server.bind_to_any_port("127.0.0.1");
        REQUIRE(port > 0);
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~LocalServer() {
        server.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }
    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port) + path; }
};

CurlOptions testOptions(int concurrent = 6) {
    using snx::core::kMiB;
    CurlOptions options =
        curlOptionsFrom(snx::core::makeRuntimeProfile(snx::core::LaunchMode::Application, 3184 * kMiB, 64 * kMiB));
    options.maxConcurrent = concurrent;
    options.retry.baseDelayMs = 20;  // quick tests
    options.retry.maxDelayMs = 40;
    return options;
}

Request get(const std::string& url) {
    Request r;
    r.url = url;
    return r;
}

HttpResult waitResult(const Future<HttpResult>& future) {  // a copy: the future may be a temporary
    REQUIRE(future.waitFor(15s));
    return future.get();
}

// Spins until the client has nothing in flight, at most 2 s (the count drops just after a future completes).
bool waitIdle(const CurlHttpClient& client) {
    for (int i = 0; i < 200 && client.inFlight() != 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    return client.inFlight() == 0;
}

// Spins until `flag` is set, at most 10 s.
bool waitFlag(const std::atomic<bool>& flag) {
    for (int i = 0; i < 1000 && !flag.load(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    return flag.load();
}

// An endless body: a chunk every few ms until the client goes away or the test ends.
void endlessBody(httplib::Response& res, const std::atomic<bool>& stop, int pauseMs) {
    res.set_chunked_content_provider("text/plain", [&stop, pauseMs](size_t, httplib::DataSink& sink) {
        if (stop.load()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pauseMs));
        static const std::string chunk(16 * 1024, 'x');
        return sink.write(chunk.data(), chunk.size());
    });
}

}  // namespace

TEST_SUITE("net") {

TEST_CASE("curl: 200 with headers and body, 404, decorator header, observer sees 401") {
    LocalServer s;
    s.server.Get("/hello", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("X-Echo", req.get_header_value("Authorization"));
        res.set_content("hello", "text/plain");
    });
    s.server.Get("/secret", [](const httplib::Request&, httplib::Response& res) { res.status = 401; });
    s.server.Post("/echo", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(req.body + "|" + req.get_header_value("Content-Type"), "text/plain");
    });
    s.start();

    CurlHttpClient client(testOptions());
    client.setRequestDecorator([](Request& r) { r.headers.push_back({"Authorization", "Bearer abc"}); });
    std::atomic<int> unauthorized{0};
    client.addResponseObserver([&](const Request&, const HttpResult& r) {
        if (r.ok() && r.value().status == 401) {
            ++unauthorized;
        }
    });

    const HttpResult& ok = waitResult(client.submit(get(s.url("/hello")), Priority::Interactive, {}));
    REQUIRE(ok.ok());
    CHECK(ok.value().status == 200);
    CHECK(ok.value().body == "hello");
    CHECK(ok.value().header("x-echo") == "Bearer abc");
    CHECK(ok.value().attempts == 1);

    CHECK(waitResult(client.submit(get(s.url("/nothing")), Priority::Interactive, {})).value().status == 404);
    CHECK(waitResult(client.submit(get(s.url("/secret")), Priority::Interactive, {})).value().status == 401);
    CHECK(unauthorized.load() == 1);

    Request post = get(s.url("/echo"));
    post.method = Method::Post;
    post.body = std::string(4000, 'a');  // over 1 KiB: no "Expect: 100-continue" pause
    post.headers = {{"Content-Type", "application/json"}, {"Bad\r\nInjected", "x"}};
    const HttpResult& echoed = waitResult(client.submit(post, Priority::Interactive, {}));
    CHECK(echoed.value().body == std::string(4000, 'a') + "|application/json");
}

TEST_CASE("curl: timeout, refused connection, redirect limit") {
    LocalServer s;
    std::atomic<bool> stop{false};
    s.server.Get("/slow", [&](const httplib::Request&, httplib::Response& res) {
        for (int i = 0; i < 100 && !stop.load(); ++i) {
            std::this_thread::sleep_for(20ms);
        }
        res.set_content("late", "text/plain");
    });
    s.server.Get("/loop", [](const httplib::Request&, httplib::Response& res) { res.set_redirect("/loop"); });
    s.server.Get("/once", [](const httplib::Request&, httplib::Response& res) { res.set_redirect("/target"); });
    s.server.Get("/target", [](const httplib::Request&, httplib::Response& res) { res.set_content("here", "text/plain"); });
    s.start();
    CurlHttpClient client(testOptions());

    Request slow = get(s.url("/slow"));
    slow.timeoutMs = 300;
    const auto began = std::chrono::steady_clock::now();
    const HttpResult& timedOut = waitResult(client.submit(slow, Priority::Interactive, {}));
    REQUIRE_FALSE(timedOut.ok());
    CHECK(timedOut.error().kind == ErrorKind::Timeout);
    CHECK(std::chrono::steady_clock::now() - began < 1500ms);  // timeouts aren't retried
    stop = true;

    const HttpResult& refused = waitResult(client.submit(get("http://127.0.0.1:1/"), Priority::Interactive, {}));
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().kind == ErrorKind::Network);

    const HttpResult& looped = waitResult(client.submit(get(s.url("/loop")), Priority::Interactive, {}));
    REQUIRE_FALSE(looped.ok());
    CHECK(looped.error().kind == ErrorKind::Network);
    const HttpResult& moved = waitResult(client.submit(get(s.url("/once")), Priority::Interactive, {}));
    CHECK(moved.value().body == "here");

    CHECK(waitResult(client.submit(get("ftp://127.0.0.1/"), Priority::Interactive, {})).error().kind == ErrorKind::Network);
}

TEST_CASE("curl: body limit enforced while streaming, and from Content-Length") {
    LocalServer s;
    std::atomic<bool> stop{false};
    s.server.Get("/endless", [&](const httplib::Request&, httplib::Response& res) { endlessBody(res, stop, 1); });
    s.server.Get("/big", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(std::string(100 * 1024, 'b'), "text/plain");
    });
    s.start();
    CurlHttpClient client(testOptions());

    Request endless = get(s.url("/endless"));
    endless.maxBodyBytes = 64 * 1024;
    const HttpResult& stopped = waitResult(client.submit(endless, Priority::Interactive, {}));  // ends at all = stopped early
    REQUIRE_FALSE(stopped.ok());
    CHECK(stopped.error().kind == ErrorKind::TooLarge);

    Request big = get(s.url("/big"));
    big.maxBodyBytes = 10 * 1024;
    CHECK(waitResult(client.submit(big, Priority::Interactive, {})).error().kind == ErrorKind::TooLarge);
    big.maxBodyBytes = 0;  // the default (8 MiB) is plenty
    CHECK(waitResult(client.submit(big, Priority::Interactive, {})).value().body.size() == 100 * 1024);
    stop = true;
}

TEST_CASE("curl: cancel a slow body, and cancel before it starts") {
    LocalServer s;
    std::atomic<bool> stop{false};
    std::atomic<bool> started{false};
    s.server.Get("/trickle", [&](const httplib::Request&, httplib::Response& res) {
        started = true;
        endlessBody(res, stop, 20);
    });
    s.start();
    CurlHttpClient client(testOptions(1));

    CancelToken token;
    auto future = client.submit(get(s.url("/trickle")), Priority::Interactive, token);
    auto queued = client.submit(get(s.url("/trickle")), Priority::Interactive, token);  // waits: only 1 at a time
    REQUIRE(waitFlag(started));
    std::this_thread::sleep_for(100ms);
    CHECK(client.inFlight() == 2);  // one running, one queued
    const auto began = std::chrono::steady_clock::now();
    token.cancel();
    CHECK(waitResult(future).error().kind == ErrorKind::Cancelled);
    CHECK(waitResult(queued).error().kind == ErrorKind::Cancelled);
    CHECK(std::chrono::steady_clock::now() - began < 1000ms);
    CHECK(waitIdle(client));
    stop = true;
}

TEST_CASE("curl: many parallel requests never exceed maxConcurrent") {
    LocalServer s;
    std::atomic<int> inFlight{0};
    std::atomic<int> peak{0};
    s.server.Get(R"(/item/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        const int now = ++inFlight;
        int seen = peak.load();
        while (now > seen && !peak.compare_exchange_weak(seen, now)) {
        }
        std::this_thread::sleep_for(30ms);
        --inFlight;
        res.set_content(req.matches[1].str(), "text/plain");
    });
    s.start();
    CurlHttpClient client(testOptions(6));

    std::vector<Future<HttpResult>> futures;
    for (int i = 0; i < 40; ++i) {
        futures.push_back(client.submit(get(s.url("/item/" + std::to_string(i))), Priority::Prefetch, {}));
    }
    for (int i = 0; i < 40; ++i) {
        const HttpResult& r = waitResult(futures[static_cast<size_t>(i)]);
        REQUIRE(r.ok());
        CHECK(r.value().body == std::to_string(i));
    }
    CHECK(peak.load() <= 6);
    CHECK(peak.load() >= 2);
    CHECK(waitIdle(client));
}

TEST_CASE("curl: queued requests start most urgent first") {
    LocalServer s;
    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> release{false};
    std::mutex orderMutex;
    std::vector<std::string> order;
    s.server.Get("/blocker", [&](const httplib::Request&, httplib::Response& res) {
        blockerStarted = true;
        for (int i = 0; i < 500 && !release.load(); ++i) {
            std::this_thread::sleep_for(10ms);
        }
        res.set_content("done", "text/plain");
    });
    s.server.Get(R"(/p/(\w+))", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(req.matches[1].str());
        res.set_content("ok", "text/plain");
    });
    s.start();
    CurlHttpClient client(testOptions(1));

    auto blocker = client.submit(get(s.url("/blocker")), Priority::Background, {});
    REQUIRE(waitFlag(blockerStarted));
    std::vector<Future<HttpResult>> futures;
    futures.push_back(client.submit(get(s.url("/p/background")), Priority::Background, {}));
    futures.push_back(client.submit(get(s.url("/p/prefetch")), Priority::Prefetch, {}));
    futures.push_back(client.submit(get(s.url("/p/interactive1")), Priority::Interactive, {}));
    futures.push_back(client.submit(get(s.url("/p/reader")), Priority::Reader, {}));
    futures.push_back(client.submit(get(s.url("/p/interactive2")), Priority::Interactive, {}));
    release = true;
    waitResult(blocker);
    for (auto& f : futures) {
        waitResult(f);
    }
    std::lock_guard<std::mutex> lock(orderMutex);
    CHECK(order == std::vector<std::string>{"interactive1", "interactive2", "reader", "prefetch", "background"});
}

TEST_CASE("curl: GETs retry on 503, POSTs only when marked idempotent") {
    LocalServer s;
    std::atomic<int> getCalls{0};
    std::atomic<int> postCalls{0};
    s.server.Get("/flaky", [&](const httplib::Request&, httplib::Response& res) {
        if (++getCalls == 1) {
            res.status = 503;
            return;
        }
        res.set_content("fine", "text/plain");
    });
    s.server.Post("/flaky", [&](const httplib::Request&, httplib::Response& res) {
        ++postCalls;
        res.status = 503;
    });
    s.server.Get("/down", [](const httplib::Request&, httplib::Response& res) { res.status = 502; });
    s.start();
    CurlHttpClient client(testOptions());

    const HttpResult& retried = waitResult(client.submit(get(s.url("/flaky")), Priority::Interactive, {}));
    CHECK(retried.value().status == 200);
    CHECK(retried.value().attempts == 2);

    Request post = get(s.url("/flaky"));
    post.method = Method::Post;
    const HttpResult& once = waitResult(client.submit(post, Priority::Interactive, {}));
    CHECK(once.value().status == 503);
    CHECK(once.value().attempts == 1);
    CHECK(postCalls.load() == 1);

    post.idempotent = true;  // e.g. a GraphQL query: safe to repeat
    const HttpResult& repeated = waitResult(client.submit(post, Priority::Interactive, {}));
    CHECK(repeated.value().status == 503);
    CHECK(repeated.value().attempts == 3);
    CHECK(postCalls.load() == 4);

    const HttpResult& gaveUp = waitResult(client.submit(get(s.url("/down")), Priority::Interactive, {}));
    CHECK(gaveUp.value().status == 502);
    CHECK(gaveUp.value().attempts == 3);  // 1 + httpRetry.maxRetries (2)
}

TEST_CASE("curl: resetConnections drops running transfers, GETs retry on fresh connections") {
    LocalServer s;
    std::atomic<int> calls{0};
    std::atomic<bool> firstStarted{false};
    std::atomic<bool> stop{false};
    s.server.Get("/after-sleep", [&](const httplib::Request&, httplib::Response& res) {
        if (++calls == 1) {
            firstStarted = true;
            for (int i = 0; i < 100 && !stop.load(); ++i) {
                std::this_thread::sleep_for(10ms);
            }
        }
        res.set_content("awake", "text/plain");
    });
    s.start();
    CurlHttpClient client(testOptions());

    auto future = client.submit(get(s.url("/after-sleep")), Priority::Interactive, {});
    REQUIRE(waitFlag(firstStarted));
    client.resetConnections();
    const HttpResult& r = waitResult(future);
    REQUIRE(r.ok());
    CHECK(r.value().body == "awake");
    CHECK(r.value().attempts == 2);
    stop = true;
    CHECK(waitResult(client.submit(get(s.url("/after-sleep")), Priority::Interactive, {})).ok());
}

TEST_CASE("curl: shutdown with jobs in flight ends every future, later submits fail at once") {
    LocalServer s;
    std::atomic<bool> stop{false};
    s.server.Get("/trickle", [&](const httplib::Request&, httplib::Response& res) { endlessBody(res, stop, 20); });
    s.start();
    auto client = std::make_unique<CurlHttpClient>(testOptions(2));

    std::vector<Future<HttpResult>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(client->submit(get(s.url("/trickle")), Priority::Background, {}));
    }
    std::this_thread::sleep_for(200ms);
    std::atomic<int> callbacks{0};
    for (auto& f : futures) {
        f.then([&](const HttpResult&) { ++callbacks; });
    }
    client->shutdown();
    for (auto& f : futures) {
        CHECK(waitResult(f).error().kind == ErrorKind::Cancelled);
    }
    CHECK(callbacks.load() == 10);
    CHECK(client->submit(get(s.url("/trickle")), Priority::Interactive, {}).get().error().kind == ErrorKind::Cancelled);
    client.reset();  // the destructor after shutdown is fine too
    stop = true;
}

}  // TEST_SUITE("net")
