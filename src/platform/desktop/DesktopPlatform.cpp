#include <unistd.h>

#include <csignal>
#include <cstdio>

#include "platform/Platform.hpp"

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace snx::platform {

namespace {

bool gSimulateAppletMode = false;
std::uint64_t gMemLimitBytes = 0;
bool gSimulateDocked = false;

// Set by the signal handler, read by the main loop. sig_atomic_t is the one type a signal handler may safely write.
volatile std::sig_atomic_t gQuitRequested = 0;

extern "C" void onQuitSignal(int) {
    gQuitRequested = 1;
}

std::uint64_t physicalMemory() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || pageSize <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
}

// In-use heap blocks plus the big blocks glibc gets with mmap (hblkhd): a decoded page or webtoon strip is one of those, and
// without it the memory-pressure guard couldn't see images at all (WP-07). The Switch's allocator has no mmap, so its uordblks
// already counts everything.
std::uint64_t allocatorInUse() {
#ifdef __GLIBC__
    const struct mallinfo2 info = mallinfo2();
    return static_cast<std::uint64_t>(info.uordblks) + static_cast<std::uint64_t>(info.hblkhd);
#else
    return 0;
#endif
}

}  // namespace

std::string_view platformName() {
    return "desktop";
}

std::string dataDir() {
    return "./data/";
}

void setDesktopTestFlags(bool simulateAppletMode, std::uint64_t memLimitBytes, bool simulateDocked) {
    gSimulateDocked = simulateDocked;
    gSimulateAppletMode = simulateAppletMode;
    gMemLimitBytes = memLimitBytes;
}

core::RuntimeInfo runtimeInfo() {
    core::RuntimeInfo info;
    info.mode = gSimulateAppletMode ? core::LaunchMode::Applet : core::LaunchMode::Application;
    info.totalMem = gMemLimitBytes != 0 ? gMemLimitBytes : physicalMemory();
    info.usedMem = allocatorInUse();
    return info;
}

void installQuitSignalHandlers() {
    std::signal(SIGINT, onQuitSignal);
    std::signal(SIGTERM, onQuitSignal);
}

bool quitRequested() {
    return gQuitRequested != 0;
}

bool networkConnected() {
    return true;  // the PC's network isn't checked: an unreachable server is reported as such
}

void setResumeHandler(void (*)()) {
    // A PC doesn't sleep under the app the way the Switch does.
}

void keepScreenOn(bool) {
    // The PC's screen saver is the PC owner's business.
}

bool handheldMode() {
    return !gSimulateDocked;
}

bool showLaunchRefused(const char* text, int, void (*onShown)()) {
    std::fputs(text, stdout);
    std::fputs("\n", stdout);
    std::fflush(stdout);
    if (onShown != nullptr) {
        onShown();
    }
    return true;
}

void exitProcessNow(int code) {
    ::_exit(code);
}

}  // namespace snx::platform
