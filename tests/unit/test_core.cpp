#include <algorithm>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/LaunchOptions.hpp"
#include "core/RuntimeInfo.hpp"
#include "core/TestEvents.hpp"
#include "core/Version.hpp"

namespace {

snx::core::LaunchOptions parse(std::vector<const char*> args) {
    args.insert(args.begin(), "suwayomi-nx");  // argv[0], the program path
    return snx::core::parseLaunchOptions(static_cast<int>(args.size()), args.data());
}

// One event line back to JSON. Also checks it is exactly one line.
nlohmann::json parseLine(const std::string& line) {
    REQUIRE(!line.empty());
    CHECK(line.back() == '\n');
    CHECK(std::count(line.begin(), line.end(), '\n') == 1);
    return nlohmann::json::parse(line);
}

}  // namespace

TEST_SUITE("core") {

TEST_CASE("the version comes from CMake and looks like major.minor.patch") {
    const std::string version(snx::core::appVersion());
    CHECK(version == SNX_TEST_EXPECTED_VERSION);
    CHECK(std::count(version.begin(), version.end(), '.') == 2);
}

TEST_CASE("the version label marks test builds") {
#ifdef SNX_TEST_HOOKS
    CHECK(snx::core::buildFlavor() == "test");
    CHECK(snx::core::versionLabel() == std::string(SNX_TEST_EXPECTED_VERSION) + " (test build)");
#else
    CHECK(snx::core::buildFlavor() == "release");
    CHECK(snx::core::versionLabel() == SNX_TEST_EXPECTED_VERSION);
#endif
}

TEST_CASE("the vendored nlohmann/json is the one we pinned (3.12.0)") {
    CHECK(NLOHMANN_JSON_VERSION_MAJOR == 3);
    CHECK(NLOHMANN_JSON_VERSION_MINOR == 12);
    const auto parsed = nlohmann::json::parse(R"({"name":"suwayomi-nx","pages":[1,2,3]})");
    CHECK(parsed.at("name") == "suwayomi-nx");
    CHECK(parsed.at("pages").size() == 3);
}

TEST_CASE("launch options: no flags means defaults") {
    const auto options = parse({});
    CHECK_FALSE(options.debugLog);
    CHECK_FALSE(options.logToFile);
    CHECK_FALSE(options.simulateAppletMode);
    CHECK(options.memLimitMb == 0);
    CHECK(options.problems.empty());
}

TEST_CASE("launch options: every known flag") {
    const auto options = parse({"-d", "--log-file", "--simulate-applet-mode", "--mem-limit=512"});
    CHECK(options.debugLog);
    CHECK(options.logToFile);
    CHECK(options.simulateAppletMode);
    CHECK(options.memLimitMb == 512);
    CHECK(options.problems.empty());
}

TEST_CASE("launch options: bad --mem-limit values are ignored and reported") {
    for (const char* bad : {"--mem-limit=", "--mem-limit=0", "--mem-limit=-5", "--mem-limit=12MB", "--mem-limit=1.5",
                            "--mem-limit=99999999999999", "--mem-limit=1048577"}) {
        CAPTURE(bad);
        const auto options = parse({bad});
        CHECK(options.memLimitMb == 0);
        REQUIRE(options.problems.size() == 1);
        CHECK(options.problems[0].find(bad) != std::string::npos);
    }
    CHECK(parse({"--mem-limit=1048576"}).memLimitMb == 1048576);  // 1 TiB is the largest accepted value
}

TEST_CASE("launch options: unknown flags are reported, the rest still works") {
    const auto options = parse({"--nope", "-d", "--mem-limit"});
    CHECK(options.debugLog);
    REQUIRE(options.problems.size() == 2);
    CHECK(options.problems[0] == "ignored unknown flag --nope");
    CHECK(options.problems[1] == "ignored unknown flag --mem-limit");
}

TEST_CASE("launch mode names match the test events (D21)") {
    CHECK(snx::core::launchModeName(snx::core::LaunchMode::Application) == "application");
    CHECK(snx::core::launchModeName(snx::core::LaunchMode::Applet) == "applet");
}

TEST_CASE("test events: every line has t and ev first, then the fields") {
    nlohmann::ordered_json fields;
    fields["name"] = "Library";
    fields["t"] = 99;     // can't overwrite the time
    fields["ev"] = "x";  // or the event name
    const std::string text = snx::core::testevents::line(1234, "screen", fields);
    CHECK(text == "{\"t\":1234,\"ev\":\"screen\",\"name\":\"Library\"}\n");
}

TEST_CASE("test events: app_start carries the runtime info") {
    snx::core::testevents::AppStart info;
    info.version = "0.0.1";
    info.build = "test";
    info.platform = "desktop";
    info.runtime.mode = snx::core::LaunchMode::Application;
    info.runtime.totalMem = 3338665984ULL;  // more than 32 bits
    info.runtime.usedMem = 1024;
    info.profile = "full";
    const auto event = parseLine(snx::core::testevents::appStart(40, info));
    CHECK(event["t"] == 40);
    CHECK(event["ev"] == "app_start");
    CHECK(event["version"] == "0.0.1");
    CHECK(event["build"] == "test");
    CHECK(event["platform"] == "desktop");
    CHECK(event["mode"] == "application");
    CHECK(event["profile"] == "full");
    CHECK(event["memTotal"] == 3338665984ULL);
    CHECK(event["memUsed"] == 1024);

    info.runtime.mode = snx::core::LaunchMode::Applet;
    info.profile = "none";
    const auto applet = parseLine(snx::core::testevents::appStart(41, info));
    CHECK(applet["mode"] == "applet");
    CHECK(applet["profile"] == "none");
}

TEST_CASE("test events: screen and idle, odd text stays one valid line") {
    CHECK(snx::core::testevents::idle(7) == "{\"t\":7,\"ev\":\"idle\"}\n");
    const auto event = parseLine(snx::core::testevents::screen(8, "two\nlines \"quoted\" 日本語"));
    CHECK(event["name"] == "two\nlines \"quoted\" 日本語");
    // Broken UTF-8 is replaced, not thrown.
    const auto broken = parseLine(snx::core::testevents::screen(9, std::string("bad \xff byte")));
    CHECK(broken["ev"] == "screen");
}

TEST_CASE("idle tracker: idle once after a few quiet frames") {
    snx::core::testevents::IdleTracker idle;
    const int quiet = snx::core::testevents::IdleTracker::kQuietFrames;
    for (int i = 1; i < quiet; i++) {
        CHECK_FALSE(idle.onFrame(false));
    }
    CHECK(idle.onFrame(false));        // the quiet spell is long enough: report
    CHECK_FALSE(idle.onFrame(false));  // and only once
    CHECK_FALSE(idle.onFrame(false));
}

TEST_CASE("idle tracker: busy frames restart the count") {
    snx::core::testevents::IdleTracker idle;
    const int quiet = snx::core::testevents::IdleTracker::kQuietFrames;
    for (int i = 1; i < quiet; i++) {
        CHECK_FALSE(idle.onFrame(false));
    }
    CHECK_FALSE(idle.onFrame(true));  // an animation started just before idle was reached
    for (int i = 1; i < quiet; i++) {
        CHECK_FALSE(idle.onFrame(false));
    }
    CHECK(idle.onFrame(false));
    CHECK_FALSE(idle.onFrame(true));  // busy again after idle was reported ...
    for (int i = 1; i < quiet; i++) {
        CHECK_FALSE(idle.onFrame(false));
    }
    CHECK(idle.onFrame(false));  // ... so the next quiet spell reports again
}

TEST_CASE("idle tracker: markBusy reports idle again without any busy frame") {
    snx::core::testevents::IdleTracker idle;
    const int quiet = snx::core::testevents::IdleTracker::kQuietFrames;
    for (int i = 1; i < quiet; i++) {
        idle.onFrame(false);
    }
    CHECK(idle.onFrame(false));
    idle.markBusy();  // a new screen that shows up without an animation
    for (int i = 1; i < quiet; i++) {
        CHECK_FALSE(idle.onFrame(false));
    }
    CHECK(idle.onFrame(false));
}

}  // TEST_SUITE("core")
