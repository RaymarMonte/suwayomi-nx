#include "app/AppletGuard.hpp"

#include <borealis.hpp>

#include "app/Logger.hpp"
#include "app/TestHooks.hpp"
#include "platform/Platform.hpp"

namespace snx::app {

namespace {
constexpr int kScreenTimeoutMs = 60000;  // D21: it closes by itself after 60 s
}  // namespace

const char* const kAppletModeText =
    "  Suwayomi NX can't run from the Album yet.\n"
    "  It needs the full memory a game gets.\n"
    "\n"
    "  To open it: on the HOME Menu, hold R and start any game.\n"
    "  Keep holding R until the Homebrew Menu appears, then choose Suwayomi NX.\n"
    "\n"
    "  Press any button or tap the screen to close.";

bool refuseAppletMode(const core::RuntimeInfo& runtime, const platform::SafeFs& fs) {
    if (runtime.mode == core::LaunchMode::Application) {
        return false;
    }
    // S12: everything that is written, is written now, before the screen. Afterwards nothing is, so closing the app from HOME
    // at any moment is harmless.
    brls::Logger::warning("launch refused: applet mode (applet type {}). Suwayomi NX runs only in application mode (D21)",
                          runtime.appletType);
    testhooks::start(fs);
    testhooks::launchRefused(runtime);
    void (*drill)() = testhooks::appletGuardDrill();  // test builds only: the null-pointer drill inside the screen
    testhooks::flushNow();
    logger::flush();
    // If the screen can't be set up it is skipped, and the app just leaves (S12). Either way nothing more is logged.
    platform::showLaunchRefused(kAppletModeText, kScreenTimeoutMs, drill);
    return true;
}

}  // namespace snx::app
