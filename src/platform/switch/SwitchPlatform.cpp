#include <malloc.h>
#include <stdio.h>
#include <switch.h>

#include <atomic>

#include "platform/Platform.hpp"

// The heap libnx set up before main(): everything between these two is ours to allocate from (D12 amendment, WP-00).
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;
// libnx's exit mode (applet.c): 1 = at exit, tell the system "I'm leaving" instead of returning to the loader (hbmenu's quit
// sets it; borealis's Platform::exitToHomeMode too). D62: set from the Album on an exit request.
extern "C" u32 __nx_applet_exit_mode;

namespace snx::platform {

std::string_view platformName() {
    return "switch";
}

std::string dataDir() {
    return "sdmc:/switch/suwayomi-nx/";
}

void setDesktopTestFlags(bool, std::uint64_t, bool) {
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
        // AppletMessage_Resume (the OnResume hook) only comes when the app asks for it; without this the hook never ran
        // (WP-18b, found on the console). It works from the Album too (WP-21's console checks); it touches nothing outside
        // this app's session.
        // A failure only means no wake check (the app's "woke from sleep" log line shows whether it works).
        (void)appletSetRestartMessageEnabled(true);
        gResumeHooked = true;
    } else if (handler == nullptr && gResumeHooked) {
        appletUnhook(&gResumeCookie);
        gResumeHooked = false;
    }
}

namespace {

AppletHookCookie gExitCookie;
bool gExitHooked = false;

// Beside borealis's own hook, which calls Application::quit() on the same message (both run from appletMainLoop()).
void onExitHook(AppletHookType hook, void*) {
    if (hook == AppletHookType_OnExitRequest) {
        __nx_applet_exit_mode = 1;
    }
}

}  // namespace

void setLeaveOnExitRequest(bool on) {
    if (on && !gExitHooked) {
        appletHook(&gExitCookie, onExitHook, nullptr);
        gExitHooked = true;
    } else if (!on && gExitHooked) {
        appletUnhook(&gExitCookie);
        gExitHooked = false;
    }
}

void keepScreenOn(bool on) {
    // "Media playback": no dimming and no auto-sleep for this application while it is set (what video players use).
    appletSetMediaPlaybackState(on);
}

bool handheldMode() {
    return appletGetOperationMode() == AppletOperationMode_Handheld;
}

std::int64_t utcOffsetSeconds(std::int64_t utcSeconds) {
    TimeCalendarTime calendar;
    TimeCalendarAdditionalInfo info;
    if (utcSeconds < 0 || R_FAILED(timeToCalendarTimeWithMyRule(static_cast<u64>(utcSeconds), &calendar, &info))) {
        return 0;
    }
    return info.offset;  // seconds east of UTC at that moment (libnx: "relative to UTC for this timezone")
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
    for (;;) {
        if (!appletMainLoop()) {
            // An exit request (HOME -> start a game, the Album closed): leave like hbmenu's quit, not back to hbloader (D62).
            __nx_applet_exit_mode = 1;
            break;
        }
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

namespace {

// setExitMode()'s values. Atomics so any thread may end the process; all of them are lock-free plain words.
std::atomic<bool> gAppletEnding{false};
std::atomic<int> gExitWaitMs{5000};
std::atomic<ExitNote> gExitNote{nullptr};
// The thread that started the applet-mode ending (INVALID_HANDLE = none yet).
std::atomic<Handle> gEndingThread{INVALID_HANDLE};

void writeNote(const char* step, std::uint32_t value) noexcept {
    if (const ExitNote fn = gExitNote.load(); fn != nullptr) {
        fn(step, value);
    }
}

void sleepMs(int ms) noexcept {
    svcSleepThread(static_cast<s64>(ms > 0 ? ms : 0) * 1000000LL);
}

// D62: what hbmenu's quit does at its very end (libnx 4.12 _appletExitProcess: ILibraryAppletSelfAccessor command 10, then
// sleep until the system ends the process). The applet manager then sees a library applet that ended normally, and the HOME
// menu carries on. Without it the HOME menu stops the system (0x290, D41). Returns only if the process should end now itself.
void tellSystemAndWait() noexcept {
    const Handle self = threadGetCurHandle();
    Handle first = INVALID_HANDLE;
    if (!gEndingThread.compare_exchange_strong(first, self)) {
        if (first == self) {
            return;  // a crash inside this ending (a second crash on the same thread): end at once
        }
        sleepMs(gExitWaitMs.load());  // another thread already told the system: wait for the end like it does
        return;
    }
    Service* session = appletGetServiceSession_LibraryAppletSelfAccessor();
    if (session == nullptr || !serviceIsActive(session)) {
        writeNote("no ILibraryAppletSelfAccessor session (applet type)", static_cast<std::uint32_t>(appletGetAppletType()));
        return;
    }
    // Noted first: if the system ends the process before the call even returns, this line still shows the ending ran.
    writeNote("sending ExitProcessAndReturn (command 10), applet type", static_cast<std::uint32_t>(appletGetAppletType()));
    // libnx's session, used as libnx uses it: one IPC message on this thread's own buffer, no allocation, no lock.
    const Result rc = serviceDispatch(session, 10);
    writeNote("ExitProcessAndReturn (command 10) result", rc);
    if (R_FAILED(rc)) {
        return;
    }
    const int waitMs = gExitWaitMs.load();
    sleepMs(waitMs);
    writeNote("still running after the wait (ms); svcExitProcess", static_cast<std::uint32_t>(waitMs));
}

}  // namespace

void setExitMode(core::LaunchMode mode, int waitMs, ExitNote note) {
    gAppletEnding.store(mode == core::LaunchMode::Applet);
    gExitWaitMs.store(waitMs > 0 ? waitMs : 0);
    gExitNote.store(note);
}

void exitProcessNow(int) {
    if (gAppletEnding.load()) {
        tellSystemAndWait();  // normally the system ends the process in there
    }
    // Horizon has no exit code here. The process (hbloader with our .nro in it) simply ends. Application mode: the console
    // goes back to the HOME menu, which says "The software was closed because an error occurred." (seen on the console in
    // WP-03c). Atmosphère's crash reporter is never involved and nothing is written (SAFETY.md).
    svcExitProcess();
}

}  // namespace snx::platform
