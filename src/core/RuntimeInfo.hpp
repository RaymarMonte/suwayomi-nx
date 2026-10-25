#pragma once

#include <cstdint>
#include <string_view>

namespace snx::core {

// How the app was started (D12, D21). Application = "hold R on a game" (the only mode the app supports for now),
// Applet = from the Album (the app will refuse to run there, WP-08).
enum class LaunchMode { Application, Applet };

// "application" or "applet": the names the test events and the log use.
std::string_view launchModeName(LaunchMode mode);

// What platform::runtimeInfo() measured at start-up. WP-08's makeRuntimeProfile() turns it into budgets.
struct RuntimeInfo {
    LaunchMode mode = LaunchMode::Application;
    std::uint64_t totalMem = 0;  // bytes the app may use: the heap libnx got (Switch), or the PC's RAM / --mem-limit (desktop)
    std::uint64_t usedMem = 0;   // bytes the allocator has handed out so far
};

}  // namespace snx::core
