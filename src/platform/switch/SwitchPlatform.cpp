#include <malloc.h>
#include <stdio.h>
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
    info.appletType = static_cast<int>(type);
    info.mode =(type == AppletType_Application || type == AppletType_SystemApplication) ? core::LaunchMode::Application
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

bool networkConnected() {
    // nifm is started before main() (borealis's userAppInit). An IP address means "on a network", internet or not: a
    // Suwayomi server at home is usually reached without one. If nifm can't say, don't claim the console is offline.
    u32 ip = 0;
    const Result rc = nifmGetCurrentIpAddress(&ip);
    return R_SUCCEEDED(rc) ? ip != 0 : true;
}

namespace {

void (*gResumeHandler)() = nullptr;
AppletHookCookie gResumeCookie;
bool gResumeHooked = false;

// libnx calls applet hooks from appletMainLoop(), which borealis runs on the main thread every frame.
void onAppletHook(AppletHookType hook, void*) {
    if (hook == AppletHookType_OnResume && gResumeHandler != nullptr) {
        gResumeHandler();
    }
}

}  // namespace

void setResumeHandler(void (*handler)()) {
    gResumeHandler = handler;
    if (handler != nullptr && !gResumeHooked) {
        appletHook(&gResumeCookie, onAppletHook, nullptr);
        gResumeHooked = true;
    } else if (handler == nullptr && gResumeHooked) {
        appletUnhook(&gResumeCookie);
        gResumeHooked = false;
    }
}

bool showLaunchRefused(const char* text, int timeoutMs, void (*onShown)()) {
    // S12: this runs in applet mode, where any crash is a system fatal. So: libnx's plain text console (a framebuffer, no
    // GPU set-up, no fonts), nothing that can abort, and if a step fails we skip the screen.
    PrintConsole* console = consoleInit(nullptr);
    if (console == nullptr) {
        return false;
    }
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);  // every controller, handheld included
    hidInitializeTouchScreen();

    printf("\x1b[8;1H%s\n", text);
    consoleUpdate(nullptr);
    if (onShown != nullptr) {
        onShown();
    }

    // The first reading is the starting point: a button still held from starting the app (A in hbmenu), or a finger
    // already on the screen, doesn't close it. Only a new press or a new touch does.
    padUpdate(&pad);
    HidTouchScreenState touch = {};
    bool touching = hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0;
    const u64 start = armGetSystemTick();
    const u64 limit = armNsToTicks(static_cast<u64>(timeoutMs > 0 ? timeoutMs : 0) * 1000000ULL);
    while (appletMainLoop()) {  // false on an exit request (HOME -> close)
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) != 0) {
            break;
        }
        const bool nowTouching = hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0;
        if (nowTouching && !touching) {
            break;
        }
        touching = nowTouching;
        if (armGetSystemTick() - start >= limit) {
            break;
        }
        consoleUpdate(nullptr);
        svcSleepThread(16000000);  // about one frame
    }
    consoleExit(nullptr);
    return true;
}

void exitProcessNow(int) {
    // Horizon has no exit code here. The process (hbloader with our .nro in it) simply ends, and the console goes back to the
    // HOME menu, which says "The software was closed because an error occurred." (seen on the console in WP-03c). Atmosphère's
    // crash reporter is never involved and nothing is written (SAFETY.md).
    svcExitProcess();
}

}  // namespace snx::platform
