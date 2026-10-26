#pragma once

#include <fstream>
#include <sstream>
#include <string>

#include <doctest/doctest.h>

// Reads a file under tests/fixtures/ (e.g. "responses/manga-details.json"). Fails the test if it's missing.
inline std::string readFixture(const std::string& relative) {
    const std::string path = std::string(SNX_FIXTURES_DIR) + "/" + relative;
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing fixture " << path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
