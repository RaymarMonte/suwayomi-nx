#include <doctest/doctest.h>

#include "net/Net.hpp"

TEST_SUITE("net") {

TEST_CASE("the net module links") {
    CHECK(snx::net::moduleName() == "net");
}

}  // TEST_SUITE("net")
