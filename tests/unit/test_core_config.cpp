#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/AppConfig.hpp"

using namespace snx::core;

namespace {

bool mentions(const std::vector<std::string>& problems, const std::string& text) {
    for (const auto& p : problems) {
        if (p.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

AppConfig customConfig() {
    AppConfig c;
    c.server.url = "http://192.168.1.10:4567/suwayomi";
    c.server.auth = AuthMode::UiLogin;
    c.server.username = "snx";
    c.server.password = "p\"\xC3\xA4ss\\word";
    c.server.accessToken = "a.b.c";
    c.server.refreshToken = "r.s.t";
    c.reader.direction = ReadingDirection::Vertical;
    c.reader.fit = PageFit::Width;
    c.reader.serverConvertsToJpeg = true;
    return c;
}

void checkSame(const AppConfig& a, const AppConfig& b) {
    CHECK(a.schemaVersion == b.schemaVersion);
    CHECK(a.server.url == b.server.url);
    CHECK(a.server.auth == b.server.auth);
    CHECK(a.server.username == b.server.username);
    CHECK(a.server.password == b.server.password);
    CHECK(a.server.accessToken == b.server.accessToken);
    CHECK(a.server.refreshToken == b.server.refreshToken);
    CHECK(a.reader.direction == b.reader.direction);
    CHECK(a.reader.fit == b.reader.fit);
    CHECK(a.reader.serverConvertsToJpeg == b.reader.serverConvertsToJpeg);
}

// A test migration: version 1 kept the URL at the top level, version 2 moved it into "server".
void moveUrlIntoServer(nlohmann::json& doc) {
    if (doc.contains("url")) {
        doc["server"]["url"] = doc["url"];
        doc.erase("url");
    }
}

void breakEverything(nlohmann::json& doc) {
    doc = nlohmann::json::array();
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("AppConfig: save then load gives the same settings, with no problems") {
        for (const AppConfig& original : {AppConfig{}, customConfig()}) {
            const std::string text = saveAppConfig(original);
            const ConfigLoad load = loadAppConfig(text);
            CHECK(load.problems.empty());
            checkSame(load.config, original);
        }
    }

    TEST_CASE("AppConfig: the saved text is pretty JSON with schemaVersion first") {
        const std::string text = saveAppConfig(customConfig());
        CHECK(text.rfind("{\n  \"schemaVersion\": 1,\n  \"server\": {", 0) == 0);
        CHECK(text.back() == '\n');
        const auto parsed = nlohmann::json::parse(text);
        CHECK(parsed["server"]["auth"] == "ui_login");
        CHECK(parsed["reader"]["direction"] == "vertical");
        CHECK(parsed["reader"]["fit"] == "width");
        // Invalid UTF-8 in a setting doesn't throw.
        AppConfig bad;
        bad.server.password = "x\xff";
        CHECK_NOTHROW(saveAppConfig(bad));
    }

    TEST_CASE("AppConfig: unreadable files give defaults and say why") {
        for (const char* text : {"", "not json", "[1, 2]", "\"text\"", "{\"server\": "}) {
            CAPTURE(text);
            const ConfigLoad load = loadAppConfig(text);
            CHECK_FALSE(load.problems.empty());
            checkSame(load.config, AppConfig{});
        }
        JsonLimits tiny;
        tiny.maxBytes = 10;
        CHECK(mentions(loadAppConfig(saveAppConfig(AppConfig{}), tiny).problems, "too big"));
    }

    TEST_CASE("AppConfig: wrong types and unknown values keep the default for that field only") {
        const ConfigLoad load = loadAppConfig(R"({"schemaVersion": 1,
            "server": {"url": 5, "auth": "simple_login", "username": "snx", "password": null, "accessToken": ["x"]},
            "reader": {"direction": 3, "fit": "height", "serverConvertsToJpeg": "yes"}, "somethingNew": 1})");
        const AppConfig& c = load.config;
        CHECK(c.server.url.empty());
        CHECK(c.server.auth == AuthMode::None);
        CHECK(c.server.username == "snx");
        CHECK(c.server.password.empty());
        CHECK(c.server.accessToken.empty());
        CHECK(c.reader.direction == ReadingDirection::RightToLeft);
        CHECK(c.reader.fit == PageFit::Height);
        CHECK_FALSE(c.reader.serverConvertsToJpeg);
        CHECK(mentions(load.problems, "server.url must be text"));
        CHECK(mentions(load.problems, "server.auth has an unknown value \"simple_login\""));
        CHECK(mentions(load.problems, "server.accessToken must be text"));
        CHECK(mentions(load.problems, "reader.direction has an unknown value 3"));
        CHECK(mentions(load.problems, "reader.serverConvertsToJpeg must be true or false"));
        CHECK(load.problems.size() == 5);  // null password and the unknown top-level field are fine

        const ConfigLoad sections = loadAppConfig(R"({"schemaVersion": 1, "server": "http://x", "reader": null})");
        CHECK(mentions(sections.problems, "server is not an object"));
        CHECK(sections.problems.size() == 1);
    }

    TEST_CASE("AppConfig: schemaVersion missing, too old, too new") {
        const ConfigLoad missing = loadAppConfig(R"({"server": {"url": "http://a"}})");
        CHECK(missing.config.server.url == "http://a");
        CHECK(mentions(missing.problems, "no schemaVersion"));

        const ConfigLoad zero = loadAppConfig(R"({"schemaVersion": 0, "server": {"url": "http://a"}})");
        CHECK(zero.config.server.url == "http://a");
        CHECK(mentions(zero.problems, "schemaVersion 0 is not valid"));

        const ConfigLoad newer = loadAppConfig(R"({"schemaVersion": 7, "server": {"url": "http://a"}, "future": {}})");
        CHECK(newer.config.server.url == "http://a");
        CHECK(newer.config.schemaVersion == kAppConfigVersion);  // saved back as this app's version
        CHECK(mentions(newer.problems, "written by a newer app (version 7"));
    }

    TEST_CASE("AppConfig: the migration hook runs the steps from the stored version up") {
        CHECK(configMigrations().size() + 1 == static_cast<std::size_t>(kAppConfigVersion));

        const std::vector<ConfigMigration> steps = {moveUrlIntoServer};
        const ConfigLoad old = loadAppConfig(R"({"schemaVersion": 1, "url": "http://old"})", {}, steps);
        CHECK(old.problems.empty());
        CHECK(old.config.schemaVersion == 2);
        CHECK(old.config.server.url == "http://old");

        // Already version 2: the step doesn't run (it would move a top-level "url" otherwise).
        const ConfigLoad current = loadAppConfig(R"({"schemaVersion": 2, "url": "http://ignored", "server": {"url": "http://b"}})",
                                                 {}, steps);
        CHECK(current.config.server.url == "http://b");

        const std::vector<ConfigMigration> broken = {breakEverything};
        const ConfigLoad failed = loadAppConfig(R"({"schemaVersion": 1, "server": {"url": "http://c"}})", {}, broken);
        CHECK(mentions(failed.problems, "migration to version 2 failed"));
        CHECK(failed.config.server.url.empty());
    }

    TEST_CASE("AppConfig: enum names round-trip") {
        for (const auto mode : {AuthMode::None, AuthMode::Basic, AuthMode::UiLogin}) {
            CHECK(authModeFromName(authModeName(mode)) == mode);
        }
        for (const auto d : {ReadingDirection::LeftToRight, ReadingDirection::RightToLeft, ReadingDirection::Vertical}) {
            CHECK(readingDirectionFromName(readingDirectionName(d)) == d);
        }
        for (const auto f : {PageFit::Screen, PageFit::Width, PageFit::Height, PageFit::Original}) {
            CHECK(pageFitFromName(pageFitName(f)) == f);
        }
        CHECK_FALSE(authModeFromName("BASIC_AUTH").has_value());
        CHECK_FALSE(readingDirectionFromName("").has_value());
        CHECK_FALSE(pageFitFromName("Screen").has_value());
        CHECK(authModeName(static_cast<AuthMode>(42)) == "none");
    }
}
