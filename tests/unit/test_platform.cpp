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
    snx::platform::setDesktopTestFlags(false, 0, false);
    CHECK(snx::platform::handheldMode());
    const auto info = snx::platform::runtimeInfo();
    CHECK(info.mode == snx::core::LaunchMode::Application);
    CHECK(info.totalMem > 64ULL * 1024 * 1024);  // any PC that builds this has more than 64 MiB
#ifndef __SANITIZE_THREAD__  // ThreadSanitizer's own allocator replaces glibc's, so mallinfo2() reads 0 there (tools/build.sh tsan)
    CHECK(info.usedMem > 0);                      // doctest itself has allocated by now
#endif
    CHECK(info.usedMem < info.totalMem);
}

TEST_CASE("desktop runtime info: the test flags (D21)") {
    snx::platform::setDesktopTestFlags(true, 512ULL * 1024 * 1024, true);
    CHECK_FALSE(snx::platform::handheldMode());  // --simulate-docked
    const auto info = snx::platform::runtimeInfo();
    CHECK(info.mode == snx::core::LaunchMode::Applet);
    CHECK(info.totalMem == 512ULL * 1024 * 1024);
    snx::platform::setDesktopTestFlags(false, 0, false);  // leave the defaults for other tests
}

TEST_CASE("desktop: SIGTERM asks for a clean quit instead of killing the process") {
    snx::platform::installQuitSignalHandlers();
    CHECK_FALSE(snx::platform::quitRequested());
    std::raise(SIGTERM);
    CHECK(snx::platform::quitRequested());
    std::signal(SIGINT, SIG_DFL);  // give both signals back to the system
    std::signal(SIGTERM, SIG_DFL);
}

namespace {
int gShown = 0;
void countShown() {
    ++gShown;
}
}  // namespace

TEST_CASE("desktop: the applet-mode guard's screen prints and returns at once, network counts as up (WP-08)") {
    gShown = 0;
    CHECK(snx::platform::showLaunchRefused("Suwayomi NX can't run from the Album yet.", 60000, countShown));
    CHECK(gShown == 1);
    CHECK(snx::platform::showLaunchRefused("again", 60000, nullptr));
    CHECK(gShown == 1);
    CHECK(snx::platform::networkConnected());
    snx::platform::setResumeHandler(countShown);  // never called on the PC
    snx::platform::setResumeHandler(nullptr);
    CHECK(gShown == 1);
    CHECK(snx::platform::runtimeInfo().appletType == -1);
}

}  // TEST_SUITE("platform")
