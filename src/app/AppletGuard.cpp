#include "app/AppletGuard.hpp"

#include <borealis.hpp>

#include "app/Logger.hpp"
#include "app/TestHooks.hpp"
#include "platform/Platform.hpp"

namespace snx::app {

namespace {
constexpr int kScreenTimeoutMs = 60000;  // S12: it closes by itself after 60 s
constexpr std::uint64_t kMiB = 1024 * 1024;
}  // namespace

const char* const kLowMemoryText =
    "  Suwayomi NX can't open from the Album right now: there isn't enough free\n"
    "  memory (a game open behind the Album takes some).\n"
    "\n"
    "  Close the game and try again, or open Suwayomi NX with R:\n"
    "  on the HOME Menu, hold R and start any game.\n"
    "  Keep holding R until the Homebrew Menu appears, then choose Suwayomi NX.\n"
    "\n"
    "  Press any button or tap the screen to close.";

bool refuseLowMemory(const core::RuntimeInfo& runtime, const core::RuntimeProfile& profile, const platform::SafeFs& fs) {
    if (!core::tooLittleMemoryForLite(runtime.mode, profile)) {
        return false;
    }
    // S12: everything that is written, is written now, before the screen. Afterwards nothing is, so closing the app from HOME
    // at any moment is harmless.
    brls::Logger::warning("launch refused: applet mode (applet type {}) with {} MiB free, Lite needs {} MiB (D62)",
                          runtime.appletType, profile.freeMem / kMiB, profile.liteMinFreeBytes / kMiB);
    testhooks::start(fs);
    testhooks::launchRefused(runtime, "low_memory");
    void (*drill)() = testhooks::appletGuardDrill();  // test builds only: the null-pointer drill inside the screen
    testhooks::flushNow();
    logger::flush();
    // If the screen can't be set up it is skipped, and the app just leaves (S12). Either way nothing more is logged.
    platform::showLaunchRefused(kLowMemoryText, kScreenTimeoutMs, drill);
    return true;
}

}  // namespace snx::app
