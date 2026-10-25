#include <doctest/doctest.h>

#include "api/Api.hpp"

TEST_SUITE("api") {

TEST_CASE("the User-Agent names the app and its version") {
    CHECK(snx::api::userAgent() == std::string("suwayomi-nx/") + SNX_TEST_EXPECTED_VERSION);
}

}  // TEST_SUITE("api")
