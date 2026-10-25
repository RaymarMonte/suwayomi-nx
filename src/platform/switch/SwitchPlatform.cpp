#include <malloc.h>
#include <switch.h>

#include "platform/Platform.hpp"

// The heap libnx set up before main(): everything between these two is ours to allocate from (D12 amendment, WP-00).
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;

namespace snx::platform {

std::string_view platformName() {
    return "switch";
}

std::string dataDir() {
    return "sdmc:/switch/suwayomi-nx/";
}

void setDesktopTestFlags(bool, std::uint64_t) {
    // Desktop only. On the Switch the real launch mode and memory always win.
}

core::RuntimeInfo runtimeInfo() {
    core::RuntimeInfo info;
    // The same check hbmenu and borealis use. Anything else, unknown values included, counts as applet mode (D21).
    const AppletType type = appletGetAppletType();
    info.mode = (type == AppletType_Application || type == AppletType_SystemApplication) ? core::LaunchMode::Application
                                                                                          : core::LaunchMode::Applet;
    info.totalMem = static_cast<std::uint64_t>(fake_heap_end - fake_heap_start);
    info.usedMem = static_cast<std::uint64_t>(mallinfo().uordblks);
    return info;
}

void installQuitSignalHandlers() {
    // No signals on the Switch: the app quits with PLUS or from HOME.
}

bool quitRequested() {
    return false;
}

void exitProcessNow(int) {
    // Horizon has no exit code here. The process (hbloader with our .nro in it) simply ends, and the console goes back to the
    // HOME menu. That's an ordinary exit, so Atmosphère's crash reporter is never involved (SAFETY.md).
    svcExitProcess();
}

}  // namespace snx::platform
