#pragma once

#include <string>
#include <string_view>

namespace snx::core {

// The app version from CMake's project(VERSION ...), e.g. "0.0.1".
std::string_view appVersion();

// "test" in builds with SNX_TEST_HOOKS (they write test events), otherwise "release".
std::string_view buildFlavor();

// What the app shows to people: "0.0.1" for release builds, "0.0.1 (test build)" for test builds.
std::string versionLabel();

}  // namespace snx::core
