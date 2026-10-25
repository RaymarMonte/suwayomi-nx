#include "core/RuntimeInfo.hpp"

namespace snx::core {

std::string_view launchModeName(LaunchMode mode) {
    switch (mode) {
        case LaunchMode::Application:
            return "application";
        case LaunchMode::Applet:
            return "applet";
    }
    return "applet";  // unknown values count as applet mode, like the guard does (D21)
}

}  // namespace snx::core
