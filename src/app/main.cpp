// suwayomi-nx entry point. WP-01: starts borealis and shows the Hello screen. WP-03: launch flags, runtime info and test events.
// Later WPs put things in front of borealis, in this order: crash handler (WP-03b), logger (WP-08), applet-mode guard (WP-08, D21).

#include <sys/stat.h>

#include <borealis.hpp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/TestHooks.hpp"
#include "core/LaunchOptions.hpp"
#include "core/Version.hpp"
#include "platform/Platform.hpp"
#include "ui/hello/HelloActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace {

constexpr std::uint64_t kBytesPerMb = 1024ULL * 1024ULL;

// Stop-gap until WP-08's logger and WP-03b's SafeFs: one fixed file inside our own folder (D10, D14), never a path
// from the command line. Returns nullptr if it can't be opened (the app then logs to stdout only).
std::FILE* openLogFile() {
    const std::string dir = snx::platform::dataDir();
    const std::string logs = dir + "logs/";
    mkdir(dir.c_str(), 0755);  // already there on the Switch (the .nro lives in it), maybe not on the PC
    mkdir(logs.c_str(), 0755);
    std::FILE* file = std::fopen((logs + "app.log").c_str(), "w");
    if (file != nullptr) {
        // Line-buffered, so the last lines survive a crash or a "close software" from HOME. Only used in test runs.
        std::setvbuf(file, nullptr, _IOLBF, 0);
    }
    return file;
}

}  // namespace

int main(int argc, char* argv[]) {
    const snx::core::LaunchOptions options = snx::core::parseLaunchOptions(argc, argv);
    if (options.debugLog) {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    }
    if (options.logToFile) {
        // Stays open until the process ends (the C library closes it), because borealis may still log while shutting down.
        if (std::FILE* file = openLogFile()) {
            brls::Logger::setLogOutput(file);
        }
    }

    brls::Logger::info("suwayomi-nx {} ({} build, {})", std::string(snx::core::appVersion()),
                       std::string(snx::core::buildFlavor()), std::string(snx::platform::platformName()));
    for (const std::string& problem : options.problems) {
        brls::Logger::warning("command line: {}", problem);
    }

    snx::platform::setDesktopTestFlags(options.simulateAppletMode, options.memLimitMb * kBytesPerMb);
    snx::platform::installQuitSignalHandlers();
    const snx::core::RuntimeInfo runtime = snx::platform::runtimeInfo();
    brls::Logger::info("launch mode: {}, memory: {} MiB, {} MiB in use", std::string(snx::core::launchModeName(runtime.mode)),
                       runtime.totalMem / kBytesPerMb, runtime.usedMem / kBytesPerMb);

    // Only the Full profile exists (D21). Applet mode gets none: WP-08's guard will refuse to run there.
    // Until then the Hello app simply carries on, and app_start says "applet" / "none".
    const char* profile = runtime.mode == snx::core::LaunchMode::Application ? "full" : "none";
    snx::app::testhooks::start(snx::platform::dataDir());
    snx::app::testhooks::appStart(runtime, profile);

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init borealis");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("app/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    // PLUS quits from every screen. The device scripts rely on it to get back to hbmenu.
    brls::Application::setGlobalQuit(true);

    brls::Application::pushActivity(new snx::ui::HelloActivity());
    // WP-08 moves this into the screens themselves, once there is more than one.
    snx::app::testhooks::screen("Hello");

    while (brls::Application::mainLoop()) {
        snx::app::testhooks::onFrame();
        if (snx::platform::quitRequested()) {
            brls::Logger::info("suwayomi-nx: quit requested by a signal");
            brls::Application::quit();
        }
    }

    brls::Logger::info("suwayomi-nx: main loop ended, exiting");
    snx::app::testhooks::finish();
    return EXIT_SUCCESS;
}
