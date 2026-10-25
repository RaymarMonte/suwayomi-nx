#include "platform/Platform.hpp"

namespace snx::platform {

std::string_view platformName() {
    return "switch";
}

std::string dataDir() {
    return "sdmc:/switch/suwayomi-nx/";
}

}  // namespace snx::platform
