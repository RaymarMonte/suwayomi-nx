#include <doctest/doctest.h>

#include "platform/Platform.hpp"

TEST_SUITE("platform") {

TEST_CASE("host tests use the desktop implementation") {
    CHECK(snx::platform::platformName() == "desktop");
}

TEST_CASE("the desktop data folder is ./data/ (D10)") {
    const std::string dir = snx::platform::dataDir();
    CHECK(dir == "./data/");
    CHECK(dir.back() == '/');
}

}  // TEST_SUITE("platform")
