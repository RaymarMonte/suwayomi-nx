#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/RuntimeInfo.hpp"

// The one interface to Switch-only (libnx) and desktop-only code. Each function has one implementation in
// platform/switch/ and one in platform/desktop/. CMake compiles only the folder that matches the build.
namespace snx::platform {

// "switch" or "desktop".
std::string_view platformName();

// The app's own folder, with a trailing slash (D10): "sdmc:/switch/suwayomi-nx/" on the Switch, "./data/" on the PC.
// The app never writes outside it (D14). From WP-03b every write goes through SafeFs, which enforces that.
std::string dataDir();

// Desktop test flags (D21): act as if started from the Album, and pretend to have only memLimitBytes of memory
// (0 = no limit). The Switch ignores both. Call once at start-up, before any other thread exists.
void setDesktopTestFlags(bool simulateAppletMode, std::uint64_t memLimitBytes);

// Launch mode and memory, measured now (ARCHITECTURE.md "Launch modes and runtime profiles").
// Switch: appletGetAppletType(), the heap libnx got, and the allocator's in-use bytes (D12 amendment).
// Desktop: application mode unless simulated, the PC's RAM (or the --mem-limit), and glibc's in-use bytes.
core::RuntimeInfo runtimeInfo();

// Desktop: Ctrl+C and SIGTERM (the E2E harness's "quit") ask the app to close normally instead of killing it.
// The main loop checks quitRequested() every frame. Switch: does nothing, and quitRequested() is always false.
void installQuitSignalHandlers();
bool quitRequested();

}  // namespace snx::platform
