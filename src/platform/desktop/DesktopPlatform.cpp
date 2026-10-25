#include "platform/Platform.hpp"

namespace snx::platform {

std::string_view platformName() {
    return "desktop";
}

std::string dataDir() {
    return "./data/";
}

}  // namespace snx::platform
