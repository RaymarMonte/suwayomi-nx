// suwayomi-nx entry point. WP-01: starts borealis and shows the Hello screen. WP-03: launch flags, runtime info and test events.
// WP-03b: crash containment and the write jail. The order matters (SAFETY.md S1, S12): the crash handler is set up first, then
// the logger, then everything else. WP-08 puts the applet-mode guard right after the logger (D21).

#include <borealis.hpp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "app/TestHooks.hpp"
#include "app/crash/Crash.hpp"
#include "core/LaunchOptions.hpp"
#include "core/Version.hpp"
#include "platform/Platform.hpp"
#include "platform/SafeFs.hpp"
#include "ui/crash/CrashActivity.hpp"
#include "ui/hello/HelloActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace {

constexpr std::uint64_t kBytesPerMb = 1024ULL * 1024ULL;

char levelLetter(brls::LogLevel level) {
    switch (level) {
        case brls::LogLevel::LOG_ERROR:
            return 'E';
        case brls::LogLevel::LOG_WARNING:
            return 'W';
        case brls::LogLevel::LOG_INFO:
            return 'I';
        case brls::LogLevel::LOG_DEBUG:
            return 'D';
        case brls::LogLevel::LOG_VERBOSE:
            return 'V';
    }
    return '?';
}

void logRefusedPath(std::string_view path, std::string_view why) {
    brls::Logger::error("SafeFs refused \"{}\": {}", std::string(path), std::string(why));
}

// Stop-gap until WP-08's logger: the log goes to stdout and, with --log-file, to logs/app.log (through SafeFs, D14).
// Every line also goes into the crash handler's ring buffer, so a crash record shows the last 64.
void setUpLogging(const snx::core::LaunchOptions& options, const snx::platform::SafeFs& fs) {
    if (options.debugLog) {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    }
    snx::platform::SafeFs::setRefusalLogger(logRefusedPath);
    brls::Logger::getLogEvent()->subscribe([](brls::Logger::TimePoint, brls::LogLevel level, std::string message) {
        snx::app::crash::pushLogLine(levelLetter(level), message);
    });
    if (options.logToFile && fs.makeDirs("logs")) {
        // Stays open until the process ends (the C library closes it), because borealis may still log while shutting down.
        if (std::FILE* file = fs.openLog("logs/app.log")) {
            // Line-buffered, so the last lines survive a crash or a "close software" from HOME. Only used in test runs.
            std::setvbuf(file, nullptr, _IOLBF, 0);
            brls::Logger::setLogOutput(file);
        }
    }
}

// One main-loop iteration: borealis's frame plus our per-frame work. False once the app is quitting.
bool runOneFrame() {
    if (!brls::Application::mainLoop()) {
        return false;
    }
    snx::app::testhooks::onFrame();
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    namespace crash = snx::app::crash;
    const snx::core::LaunchOptions options = snx::core::parseLaunchOptions(argc, argv);
    snx::platform::setDesktopTestFlags(options.simulateAppletMode, options.memLimitMb * kBytesPerMb);

    // 1. Crash containment, before anything that could crash (S1). It also files away the last run's crash records.
    const snx::platform::SafeFs fs(snx::platform::dataDir());
    const crash::LastRun lastRun = crash::install(fs, snx::platform::runtimeInfo().mode);

    // 2. The logger.
    setUpLogging(options, fs);
    brls::Logger::info("suwayomi-nx {} ({} build, {})", std::string(snx::core::appVersion()),
                       std::string(snx::core::buildFlavor()), std::string(snx::platform::platformName()));
    for (const std::string& problem : options.problems) {
        brls::Logger::warning("command line: {}", problem);
    }

    // Safe mode after a crash (S1): caches cleared, and (from WP-08 on) memory budgets halved, prefetch off, last screen not
    // restored. Outside safe mode only the leftovers of interrupted cache writes go (S5).
    if (lastRun.crashed) {
        brls::Logger::warning("safe mode: the last run crashed ({}: {}). Crash record: {}", lastRun.type, lastRun.what,
                              lastRun.recordPath);
        fs.removeTree("cache");
    } else {
        fs.removeTempFiles("cache");
    }

    snx::platform::installQuitSignalHandlers();
    const snx::core::RuntimeInfo runtime = snx::platform::runtimeInfo();
    brls::Logger::info("launch mode: {}, memory: {} MiB, {} MiB in use", std::string(snx::core::launchModeName(runtime.mode)),
                       runtime.totalMem / kBytesPerMb, runtime.usedMem / kBytesPerMb);

    // Only the Full profile exists (D21). Applet mode gets none: WP-08's guard will refuse to run there.
    // Until then the Hello app simply carries on, and app_start says "applet" / "none".
    const char* profile = runtime.mode == snx::core::LaunchMode::Application ? "full" : "none";
    snx::app::testhooks::start(fs);
    snx::app::testhooks::appStart(runtime, profile, lastRun.crashed, lastRun.recordPath);

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init borealis");
        crash::finish();
        return EXIT_FAILURE;
    }
    // After a crash on the main thread, borealis's task thread must stop before exit() (crash/README.md).
    crash::addExitStep([] { brls::Threading::stop(); });
    brls::Application::createWindow("app/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);

    // PLUS quits from every screen. The device scripts rely on it to get back to hbmenu.
    brls::Application::setGlobalQuit(true);

    brls::Application::pushActivity(new snx::ui::HelloActivity());
    // WP-08 moves this into the screens themselves, once there is more than one.
    snx::app::testhooks::screen("Hello");
    if (lastRun.crashed) {
        brls::Application::notify("crash/last_time"_i18n);
    }

    // The watchdog (S8) pauses while the app is in the background: borealis keeps the loop running there, but slowly.
    brls::Application::getWindowFocusChangedEvent()->subscribe([](bool focused) { crash::setFocused(focused); });
    crash::startWatchdog();

    bool running = true;
    bool crashScreenShown = false;
    while (running) {
        // S1: an exception that escapes a frame is a crash too. It gets a record and the crash screen, not a dead app.
        try {
            running = runOneFrame();
        } catch (const std::exception& e) {
            crash::recordMainLoopException(e.what());
        } catch (...) {
            crash::recordMainLoopException("an exception of an unknown type");
        }
        crash::beat();
        if (running && crash::crashScreenWanted() && !crashScreenShown) {
            crashScreenShown = true;
            brls::Application::pushActivity(new snx::ui::CrashActivity(crash::recordName()));
            snx::app::testhooks::screen("Crash");
        }
        if (running && snx::platform::quitRequested()) {
            brls::Logger::info("suwayomi-nx: quit requested by a signal");
            brls::Application::quit();
        }
    }

    // borealis stopped its own threads while quitting, so the crash exit steps must not run again.
    crash::clearExitSteps();
    brls::Logger::info("suwayomi-nx: main loop ended, exiting");
    snx::app::testhooks::finish();
    crash::finish();
    if (crashScreenShown) {
        crash::endAfterCrashScreen();  // the Switch ends the process here (D13 as amended in WP-03c)
    }
    return crashScreenShown ? EXIT_FAILURE : EXIT_SUCCESS;
}
