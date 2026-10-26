// WP-08: the pure parts of the app shell: the log buffer, config save timing, the new test config keys and test events.

#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/LogBuffer.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/SaveScheduler.hpp"
#include "core/TestConfig.hpp"
#include "core/TestEvents.hpp"

using namespace snx::core;

TEST_SUITE("core") {

TEST_CASE("log: one entry is one line, with time and level first") {
    CHECK(formatLogLine(1234, 'I', "setup: testing") == "1234 I setup: testing\n");
    CHECK(formatLogLine(0, 'E', "two\nlines\r\nhere\r") == "0 E two | lines | here | \n");
    CHECK(formatLogLine(5, 'W', "") == "5 W \n");
}

TEST_CASE("log buffer: waits for flushMs, then hands over everything at once") {
    LogBuffer log({1024, 2000});
    CHECK_FALSE(log.due(10'000));  // nothing waits
    log.add(100, 'I', "a");
    log.add(150, 'W', "b");
    CHECK(log.due(2000));  // 2 s since the start
    CHECK(log.take(2000) == "100 I a\n150 W b\n");
    CHECK(log.empty());
    log.add(2100, 'I', "c");
    CHECK_FALSE(log.due(3999));  // the wait starts again at the take
    CHECK(log.due(4000));
}

TEST_CASE("log buffer: a failed write goes back in front, and the waiting text stays small") {
    LogBuffer log({400, 1000});  // at most 100 bytes wait
    log.add(1, 'I', "first");
    std::string text = log.take(1000);
    log.add(2, 'I', "second");
    log.giveBack(text);
    CHECK(log.take(2000) == "1 I first\n2 I second\n");
    for (int i = 0; i < 50; ++i) {
        log.add(i, 'I', "0123456789");
    }
    CHECK(log.take(3000).size() <= 100);
    CHECK(log.droppedLines() > 0);
}

TEST_CASE("log buffer: rotation before the file would pass its size, never for an empty file") {
    LogBuffer log({1000, 1000});
    CHECK_FALSE(log.shouldRotate(0, 5000));
    CHECK_FALSE(log.shouldRotate(900, 100));
    CHECK(log.shouldRotate(900, 101));
}

TEST_CASE("save scheduler: waits for quiet, but never more than 4 x the delay") {
    SaveScheduler save(2000);
    CHECK_FALSE(save.pending());
    CHECK_FALSE(save.due(100'000));
    save.changed(1000);
    CHECK(save.pending());
    CHECK_FALSE(save.due(2999));
    CHECK(save.due(3000));
    save.saved();
    CHECK_FALSE(save.due(10'000));

    // A change every second keeps it waiting, until 8 s after the first one.
    for (int t = 20'000; t < 28'000; t += 1000) {
        save.changed(t);
        CHECK_FALSE(save.due(t + 500));
    }
    CHECK(save.due(28'000));
}

TEST_CASE("runtime profile: the app's own SD files have limits too") {
    const RuntimeProfile p = makeRuntimeProfile(LaunchMode::Application, 3184 * kMiB, 64 * kMiB);
    CHECK(p.logFileBytes == 1 * kMiB);
    CHECK(p.logFlushMs == 2000);
    CHECK(p.configSaveDelayMs == 2000);
}

TEST_CASE("test config: server login, showSetup, diagnostics") {
    const auto config = testconfig::parse(R"({"server": {"url": "http://10.0.0.2:4568/", "auth": "ui_login",
        "username": "snx", "password": "pw"}, "showSetup": true, "diagnostics": {"burst": 20}})");
    CHECK(config.problems.empty());
    CHECK(config.serverUrl == "http://10.0.0.2:4568");
    REQUIRE(config.serverAuth.has_value());
    CHECK(*config.serverAuth == AuthMode::UiLogin);
    CHECK(config.serverUsername == "snx");
    CHECK(config.serverPassword == "pw");
    CHECK(config.showSetup);
    CHECK(config.diagnostics);
    CHECK(config.diagnosticsBurst == 20);

    const auto plain = testconfig::parse(R"({"diagnostics": true})");
    CHECK(plain.diagnostics);
    CHECK(plain.diagnosticsBurst == 0);
    CHECK_FALSE(plain.showSetup);
    CHECK_FALSE(plain.serverAuth.has_value());

    const auto bench = testconfig::parse(R"({"imageBench": {"paths": ["/a"], "repeat": 1}})");
    CHECK(bench.diagnostics);  // the benchmark runs on the diagnostics screen

    for (const char* bad : {R"({"server": {"auth": "cookie"}})", R"({"server": {"username": 5}})", R"({"showSetup": 1})",
                            R"({"diagnostics": {"burst": 1000}})", R"({"diagnostics": "yes"})"}) {
        CAPTURE(bad);
        const auto c = testconfig::parse(bad);
        CHECK(c.problems.size() == 1);
        CHECK_FALSE(c.serverAuth.has_value());
        CHECK(c.diagnosticsBurst == 0);
    }
    const std::string longName(300, 'x');
    CHECK(testconfig::parse(R"({"server": {"password": ")" + longName + "\"}}").serverPassword.empty());
}

TEST_CASE("test events: launch_refused, mem, screen params, app_start budgets") {
    RuntimeInfo runtime;
    runtime.mode = LaunchMode::Applet;
    runtime.appletType = 2;
    runtime.totalMem = 1000;
    runtime.usedMem = 400;
    auto refused = nlohmann::json::parse(testevents::launchRefused(40, runtime));
    CHECK(refused["ev"] == "launch_refused");
    CHECK(refused["mode"] == "applet");
    CHECK(refused["appletType"] == 2);

    auto mem = nlohmann::json::parse(testevents::mem(9000, runtime));
    CHECK(mem["ev"] == "mem");
    CHECK(mem["used"] == 400);
    CHECK(mem["total"] == 1000);
    CHECK(mem["free"] == 600);
    runtime.usedMem = 2000;  // more than the total (the PC under --mem-limit)
    CHECK(nlohmann::json::parse(testevents::mem(1, runtime))["free"] == 0);

    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["mode"] = "application";
    auto screen = nlohmann::json::parse(testevents::screen(5, "Settings", params));
    CHECK(screen["name"] == "Settings");
    CHECK(screen["params"]["mode"] == "application");

    testevents::AppStart start;
    start.imageBudget = 512;
    start.httpConcurrency = 6;
    auto app = nlohmann::json::parse(testevents::appStart(0, start));
    CHECK(app["imageBudget"] == 512);
    CHECK(app["httpConcurrency"] == 6);
    CHECK(app["decodeThreads"] == 0);
}

}  // TEST_SUITE
