// suwayomi-nx entry point. WP-01: starts borealis and shows the Hello screen.
// Later WPs put things in front of borealis, in this order: crash handler (WP-03b), logger + test events (WP-03),
// applet-mode guard (WP-08, D21).

#include <sys/stat.h>

#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/Version.hpp"
#include "platform/Platform.hpp"
#include "ui/hello/HelloActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace {

// Command-line flags (desktop command line, or nxlink --args "..." on the Switch).
struct Options {
    bool debugLog = false;   // -d: debug log level
    bool logToFile = false;  // --log-file: also write the log to <data>/logs/app.log, so device runs can read it back
};

Options parseOptions(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            options.debugLog = true;
        } else if (std::strcmp(argv[i], "--log-file") == 0) {
            options.logToFile = true;
        }
    }
    return options;
}

// Stop-gap until WP-03's logger and WP-03b's SafeFs: one fixed file inside our own folder (D10, D14), never a path
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
    const Options options = parseOptions(argc, argv);
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

    while (brls::Application::mainLoop()) {
    }

    brls::Logger::info("suwayomi-nx: main loop ended, exiting");
    return EXIT_SUCCESS;
}
