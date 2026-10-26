#include <csignal>

#include <doctest/doctest.h>

#include "platform/Platform.hpp"

TEST_SUITE("platform") {

TEST_CASE("host tests use the desktop implementation") {
    CHECK(snx::platform::platformName() == "desktop");
}

TEST_CASE("the desktop data folder is ./data/ (D10)") {
    const std::string dir = snx::platform::dataDir();
    CHECK(dir == "./data/");
    CHECK(dir.back() == '/');
}

TEST_CASE("desktop runtime info: application mode with the PC's memory by default") {
    snx::platform::setDesktopTestFlags(false, 0);
    const auto info = snx::platform::runtimeInfo();
    CHECK(info.mode == snx::core::LaunchMode::Application);
    CHECK(info.totalMem > 64ULL * 1024 * 1024);  // any PC that builds this has more than 64 MiB
#ifndef __SANITIZE_THREAD__  // ThreadSanitizer's own allocator replaces glibc's, so mallinfo2() reads 0 there (tools/build.sh tsan)
    CHECK(info.usedMem > 0);                      // doctest itself has allocated by now
#endif
    CHECK(info.usedMem < info.totalMem);
}

TEST_CASE("desktop runtime info: the test flags (D21)") {
    snx::platform::setDesktopTestFlags(true, 512ULL * 1024 * 1024);
    const auto info = snx::platform::runtimeInfo();
    CHECK(info.mode == snx::core::LaunchMode::Applet);
    CHECK(info.totalMem == 512ULL * 1024 * 1024);
    snx::platform::setDesktopTestFlags(false, 0);  // leave the defaults for other tests
}

TEST_CASE("desktop: SIGTERM asks for a clean quit instead of killing the process") {
    snx::platform::installQuitSignalHandlers();
    CHECK_FALSE(snx::platform::quitRequested());
    std::raise(SIGTERM);
    CHECK(snx::platform::quitRequested());
    std::signal(SIGINT, SIG_DFL);  // give both signals back to the system
    std::signal(SIGTERM, SIG_DFL);
}

}  // TEST_SUITE("platform")
