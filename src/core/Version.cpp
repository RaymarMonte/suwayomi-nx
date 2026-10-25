#include "core/Version.hpp"

#ifndef SNX_VERSION
#error "SNX_VERSION must be set by CMake (src/core/CMakeLists.txt)"
#endif

namespace snx::core {

std::string_view appVersion() {
    return SNX_VERSION;
}

std::string_view buildFlavor() {
#ifdef SNX_TEST_HOOKS
    return "test";
#else
    return "release";
#endif
}

std::string versionLabel() {
    std::string label(appVersion());
    if (buildFlavor() == "test") {
        label += " (test build)";
    }
    return label;
}

}  // namespace snx::core
