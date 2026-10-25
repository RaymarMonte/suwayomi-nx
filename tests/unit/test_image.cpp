#include <doctest/doctest.h>

#include "image/Image.hpp"

TEST_SUITE("image") {

TEST_CASE("the image module links") {
    CHECK(snx::image::moduleName() == "image");
}

}  // TEST_SUITE("image")
