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

// Is the console on a network right now (WP-08)? Switch: nifm reports an IP address (Wi-Fi or wired; a home network without
// internet counts). Desktop: always true. Only used to word an "offline" error ("The Switch isn't connected to a network").
bool networkConnected();

// Sleep (WP-08): `handler` runs on the main thread after the console wakes up (Switch: libnx's OnResume applet hook, run by
// borealis's loop through appletMainLoop). The app resets the HTTP client's connections there (D37). Desktop: never called.
// nullptr removes it (main() does that before it returns). Main thread only.
void setResumeHandler(void (*handler)());

// The applet-mode guard's screen (D21, SAFETY.md S12): shows `text` until any button, a new tap, an exit request or
// `timeoutMs`, then returns. Switch: libnx's text console (a plain framebuffer: no GPU set-up, no fonts to load). Desktop:
// prints the text and returns at once. `onShown`, if not nullptr, runs once right after the text is first on screen (test
// builds: the crash drill inside the screen). False if the screen couldn't be set up (then nothing was shown and onShown
// didn't run). Never throws, never aborts. `text` uses "\n" for line breaks (at most 76 characters a line).
bool showLaunchRefused(const char* text, int timeoutMs, void (*onShown)());

// Ends the whole process at once: no destructors, no atexit handlers, nothing more of our code runs. Only for the crash path
// when a normal exit() can't work (a second crash, a stalled main loop, an exit that hangs: src/app/crash/README.md).
// Switch: svcExitProcess() (the process ends like a closed game, never through Atmosphère's crash reporter). Desktop: _exit(code).
[[noreturn]] void exitProcessNow(int code);

}  // namespace snx::platform
