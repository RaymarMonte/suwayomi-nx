#include <doctest/doctest.h>
#include <httplib/httplib.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "ApiTestKit.hpp"
#include "Fixtures.hpp"
#include "api/SuwayomiClient.hpp"
#include "core/RuntimeProfile.hpp"
#include "net/CurlHttpClient.hpp"

// SuwayomiClient on the real curl client against a tiny ui_login "server" on 127.0.0.1 (cpp-httplib): calls start on several
// threads, answers arrive on the network thread. The point is the token renewal's single flight under real threads, so this
// file matters most under ThreadSanitizer (tools/build.sh tsan).
using namespace snx::api;
using namespace std::chrono_literals;

TEST_SUITE("api") {

TEST_CASE("curl: five calls from five threads hit an expired token at once, and share one refresh") {
    const std::string categories = readFixture("responses/app-categories.json");
    const std::string unauthorized = readFixture("responses/unauthorized.json");
    std::atomic<int> refreshes{0};
    std::atomic<int> calls{0};

    httplib::Server server;
    server.new_task_queue = [] { return new httplib::ThreadPool(16); };
    server.Post("/api/graphql", [&](const httplib::Request& req, httplib::Response& res) {
        if (operationOf(req.body) == "RefreshToken") {
            refreshes++;
            std::this_thread::sleep_for(50ms);  // slow enough that the other refused calls arrive meanwhile
            res.set_content(R"({"data":{"refreshToken":{"accessToken":"new"}}})", "application/json");
            return;
        }
        calls++;
        res.set_content(req.get_header_value("Authorization") == "Bearer new" ? categories : unauthorized,
                        "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread serverThread([&] { server.listen_after_bind(); });
    server.wait_until_ready();

    {
        const snx::core::RuntimeProfile profile;
        snx::net::CurlHttpClient http(snx::net::curlOptionsFrom(profile));
        snx::core::ServerSettings settings;
        settings.url = "http://127.0.0.1:" + std::to_string(port);
        settings.auth = snx::core::AuthMode::UiLogin;
        settings.accessToken = "old";
        settings.refreshToken = "refresh";
        std::atomic<int> saved{0};
        ClientOptions options = clientOptionsFrom(profile);
        options.onSettingsChanged = [&saved](const snx::core::ServerSettings& changed) {
            if (changed.accessToken == "new") {
                saved++;  // runs on the network thread: no doctest macros here
            }
        };
        SuwayomiClient client(http, settings, options);

        std::atomic<int> ready{0};
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&] {
                ready++;
                while (ready.load() < 5) {
                    std::this_thread::yield();  // start all five together
                }
                auto future = client.categories(snx::net::CancelToken());
                if (future.waitFor(10s) && future.get().ok() && future.get().value().size() >= 3) {
                    ok++;
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        CHECK(ok.load() == 5);
        CHECK(refreshes.load() == 1);
        CHECK(saved.load() == 1);
        CHECK(calls.load() >= 6);  // at least one refused call, then five that went through
        CHECK(client.settings().accessToken == "new");
    }

    server.stop();
    serverThread.join();
}

}  // TEST_SUITE("api")
