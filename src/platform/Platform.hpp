#pragma once

#include <string>
#include <string_view>

// The one interface to Switch-only (libnx) and desktop-only code. Each function has one implementation in
// platform/switch/ and one in platform/desktop/. CMake compiles only the folder that matches the build.
namespace snx::platform {

// "switch" or "desktop".
std::string_view platformName();

// The app's own folder, with a trailing slash (D10): "sdmc:/switch/suwayomi-nx/" on the Switch, "./data/" on the PC.
// The app never writes outside it (D14). From WP-03b every write goes through SafeFs, which enforces that.
std::string dataDir();

}  // namespace snx::platform
