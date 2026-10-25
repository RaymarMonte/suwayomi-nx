#include <algorithm>
#include <string>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/Version.hpp"

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

}  // TEST_SUITE("core")
