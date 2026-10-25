#include <unistd.h>

#include <csignal>

#include "platform/Platform.hpp"

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace snx::platform {

namespace {

bool gSimulateAppletMode = false;
std::uint64_t gMemLimitBytes = 0;

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

std::uint64_t allocatorInUse() {
#ifdef __GLIBC__
    return static_cast<std::uint64_t>(mallinfo2().uordblks);
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

void setDesktopTestFlags(bool simulateAppletMode, std::uint64_t memLimitBytes) {
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

void exitProcessNow(int code) {
    ::_exit(code);
}

}  // namespace snx::platform
