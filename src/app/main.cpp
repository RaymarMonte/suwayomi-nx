// suwayomi-nx entry point. The order matters (SAFETY.md S1, S12, D21):
//   1. crash containment (WP-03b), first of all,
//   2. the logger (WP-08), so even the applet-mode guard's one line is kept,
//   3. the applet-mode guard (WP-08): in applet mode the app shows D21's text and leaves, before anything else runs,
//   4. config, borealis, the HTTP client (WP-05), the Suwayomi client (WP-06), the image loader (WP-07), all gathered in one
//      AppContext that every screen gets (WP-08), then the main screen and the main loop.

#include <borealis.hpp>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "api/SuwayomiClient.hpp"
#include "app/AppContext.hpp"
#include "app/AppletGuard.hpp"
#include "app/ConfigStore.hpp"
#include "app/Logger.hpp"
#include "app/TestHooks.hpp"
#include "app/crash/Crash.hpp"
#include "core/LaunchOptions.hpp"
#include "core/MemoryPressure.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/Version.hpp"
#include "image/ImageLoader.hpp"
#include "net/CurlHttpClient.hpp"
#include "platform/Platform.hpp"
#include "platform/SafeFs.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/crash/CrashActivity.hpp"
#include "ui/diagnostics/DiagnosticsActivity.hpp"
#include "ui/main/MainActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace {

constexpr std::uint64_t kBytesPerMb = 1024ULL * 1024ULL;

// After the console wakes up, kept-alive connections may be dead: start fresh ones (D37). Set while the client lives.
snx::net::HttpClient* gResumeHttp = nullptr;
void onResumeFromSleep() {
    brls::Logger::info("app: woke from sleep, resetting network connections");
    if (gResumeHttp != nullptr) {
        gResumeHttp->resetConnections();
    }
}

// One main-loop iteration: borealis's frame plus our per-frame work. False once the app is quitting.
bool runOneFrame(snx::app::ConfigStore& config) {
    if (!brls::Application::mainLoop()) {
        return false;
    }
    snx::app::testhooks::onFrame();
    config.onFrame();
    snx::app::logger::onFrame();
    return true;
}

// Test builds: the diagnostics screen (WP-05's server check and request burst, WP-07's image benchmark) over the main screen.
void openDiagnostics(snx::app::AppContext& context) {
    namespace testhooks = snx::app::testhooks;
    snx::ui::ImageBench bench;
    const std::string url = testhooks::serverUrl();
    if (!url.empty()) {
        for (const std::string& path : testhooks::imageBenchPaths()) {
            bench.urls.push_back(url + path);
        }
        bench.repeat = testhooks::imageBenchRepeat();
        bench.report = [](const nlohmann::ordered_json& summary) { testhooks::write("image_bench", summary); };
    }
    brls::Application::pushActivity(new snx::ui::DiagnosticsActivity(&context.http, url, context.profile.json,
                                                                      &context.images, std::move(bench),
                                                                      testhooks::diagnosticsBurst()));
}

}  // namespace

int main(int argc, char* argv[]) {
    namespace app = snx::app;
    namespace crash = snx::app::crash;
    namespace testhooks = snx::app::testhooks;
    const snx::core::LaunchOptions options = snx::core::parseLaunchOptions(argc, argv);
    snx::platform::setDesktopTestFlags(options.simulateAppletMode, options.memLimitMb * kBytesPerMb, options.simulateDocked);

    // 1. Crash containment, before anything that could crash (S1). It also files away the last run's crash records.
    const snx::platform::SafeFs fs(snx::platform::dataDir());
    const snx::core::RuntimeInfo runtime = snx::platform::runtimeInfo();
    const crash::LastRun lastRun = crash::install(fs, runtime.mode);

    // 2. The logger. The profile is pure math on what was just measured; it's built once and used everywhere (D21: Full).
    // Safe mode after a crash (S1): memory budgets halved, prefetch off.
    const snx::core::RuntimeProfile profile =
        snx::core::makeRuntimeProfile(runtime.mode, runtime.totalMem, runtime.usedMem, lastRun.crashed);
    app::logger::start(fs, profile, options.debugLog);
    brls::Logger::info("app: suwayomi-nx {} ({} build, {})", std::string(snx::core::appVersion()),
                       std::string(snx::core::buildFlavor()), std::string(snx::platform::platformName()));

    // 3. The applet-mode guard (D21, SAFETY.md S12): nothing else runs in applet mode, and nothing is written after it.
    if (app::refuseAppletMode(runtime, fs)) {
        return EXIT_SUCCESS;
    }

    for (const std::string& problem : options.problems) {
        brls::Logger::warning("app: command line: {}", problem);
    }
    // Safe mode also clears the caches (S1). Outside safe mode only the leftovers of interrupted cache writes go (S5).
    if (lastRun.crashed) {
        brls::Logger::warning("app: safe mode: the last run crashed ({}: {}). Crash record: {}", lastRun.type, lastRun.what,
                              lastRun.recordPath);
        fs.removeTree("cache");
    } else {
        fs.removeTempFiles("cache");
    }
    snx::platform::installQuitSignalHandlers();
    brls::Logger::info("app: launch mode {}, memory {} MiB, {} MiB in use, profile {}{}",
                       std::string(snx::core::launchModeName(runtime.mode)), runtime.totalMem / kBytesPerMb,
                       runtime.usedMem / kBytesPerMb, std::string(profile.name), profile.safeMode ? " (safe mode)" : "");
    testhooks::start(fs);
    testhooks::appStart(runtime, profile, lastRun.crashed, lastRun.recordPath);

    // 4. config.json, or the test config's server (test builds; "showSetup" shows it in the setup screen instead).
    app::ConfigStore config(fs, profile);
    config.load();
    if (const auto server = testhooks::testServer(); server && !testhooks::showSetup()) {
        brls::Logger::info("app: using the test config's server {}", server->url);
        config.useServer(*server);
    }

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;
    if (!brls::Application::init()) {
        brls::Logger::error("app: unable to init borealis");
        app::logger::stop();
        crash::finish();
        return EXIT_FAILURE;
    }
    // After a crash on the main thread, borealis's task thread must stop before exit() (crash/README.md).
    crash::addExitStep([] { brls::Threading::stop(); });
    brls::Application::registerXMLView("TextFieldCell", snx::ui::TextFieldCell::create);
    // D16: touch targets at least 72 px high. Our cells and buttons set it in their layouts; the tab list is borealis's.
    brls::Application::getStyle().addMetric("brls/sidebar/item_height", 72.0f);
    brls::Application::createWindow("app/title"_i18n);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    testhooks::steadyScreenshots();

    // PLUS quits from every screen. The device scripts rely on it to get back to hbmenu.
    brls::Application::setGlobalQuit(true);

    // The HTTP client (WP-05, D37): one network thread, every limit from the runtime profile. It lives until main() ends.
    // Created after borealis, because on the Switch borealis starts the socket service.
    snx::net::CurlHttpClient http(snx::net::curlOptionsFrom(profile));
    testhooks::watchNet(http);

    // The Suwayomi client (WP-06, D38): one per HttpClient, results on the main thread, changed logins saved to config.json.
    snx::api::ClientOptions clientOptions = snx::api::clientOptionsFrom(profile);
    clientOptions.deliver = [](std::function<void()> task) { brls::sync(std::move(task)); };
    clientOptions.onSettingsChanged = [&config](const snx::core::ServerSettings& server) {
        config.setServer(server);
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["auth"] = std::string(snx::core::authModeName(server.auth));
        fields["hasAccessToken"] = !server.accessToken.empty();
        fields["hasRefreshToken"] = !server.refreshToken.empty();
        testhooks::write("login", fields);
    };
    snx::api::SuwayomiClient client(http, config.config().server, clientOptions);

    // The image loader (WP-07, D39): decode threads, the disk cache under cache/images, the memory-pressure guard. Every
    // limit from the profile. Results reach the screens through brls::sync; its log lines too (they come from its threads).
    snx::image::LoaderOptions imageOptions = snx::image::loaderOptionsFrom(profile);
    imageOptions.deliver = [](std::function<void()> task) { brls::sync(std::move(task)); };
    imageOptions.fs = testhooks::diskCache() ? &fs : nullptr;  // test builds can turn the SD-card cache off (WP-12)
    imageOptions.memory = []() -> std::optional<snx::core::MemorySample> {
        const snx::core::RuntimeInfo info = snx::platform::runtimeInfo();
        return snx::core::MemorySample{info.usedMem, info.totalMem};
    };
    imageOptions.log = [](const std::string& line) { brls::sync([line] { brls::Logger::info("image: {}", line); }); };
    snx::image::ImageLoader images(http, imageOptions);
    testhooks::watchImages(images);

    // GPU memory for the images on screen (S7): every texture a screen makes is reserved here first (WP-09).
    snx::image::TextureBudget textures(profile.textureBudgetBytes);

    app::AppContext context{fs, runtime, profile, lastRun, config, http, client, images, textures};
    // Test builds' idle rule: no network request or image load pending, and no animation (TESTING.md).
    testhooks::watchApp([&http, &images] { return http.inFlight() > 0 || images.activeJobs() > 0; });

    // HOME, sleep, another app on top: write what is waiting now (S5), and pause the watchdog (borealis keeps the loop
    // running in the background, but slowly). Waking up from sleep: fresh network connections.
    brls::Application::getWindowFocusChangedEvent()->subscribe([&config](bool focused) {
        crash::setFocused(focused);
        if (!focused) {
            brls::Logger::info("app: in the background, saving");
            config.flush();
            app::logger::flush();
        }
    });
    gResumeHttp = &http;
    snx::platform::setResumeHandler(onResumeFromSleep);

    brls::Application::pushActivity(new snx::ui::MainActivity(context));
    if (testhooks::diagnostics()) {
        brls::sync([&context] { openDiagnostics(context); });
    }
    if (lastRun.crashed) {
        brls::Application::notify("crash/last_time"_i18n);
    }
    crash::startWatchdog();

    bool running = true;
    bool crashScreenShown = false;
    while (running) {
        // S1: an exception that escapes a frame is a crash too. It gets a record and the crash screen, not a dead app.
        try {
            running = runOneFrame(config);
        } catch (const std::exception& e) {
            crash::recordMainLoopException(e.what());
        } catch (...) {
            crash::recordMainLoopException("an exception of an unknown type");
        }
        crash::beat();
        if (running && crash::crashScreenWanted() && !crashScreenShown) {
            crashScreenShown = true;
            brls::Application::pushActivity(new snx::ui::CrashActivity(crash::recordName()));
            testhooks::screen("Crash");
        }
        if (running && snx::platform::quitRequested()) {
            brls::Logger::info("app: quit requested by a signal");
            brls::Application::quit();
        }
    }

    // The exit steps (S5: within 1.5 s): settings first, then no more cache writes, then the network, then the log.
    // borealis stopped its own threads while quitting, so the crash exit steps must not run again.
    crash::clearExitSteps();
    brls::Logger::info("app: main loop ended, exiting");
    config.flush();
    images.shutdown();  // its loads end, its threads stop, no more cache writes (S5)
    snx::platform::setResumeHandler(nullptr);
    gResumeHttp = nullptr;
    http.shutdown();    // ends what is still queued or running, and the network thread
    testhooks::finish();
    app::logger::stop();
    crash::finish();
    if (crashScreenShown) {
        crash::endAfterCrashScreen();  // the Switch ends the process here (D13 as amended in WP-03c)
    }
    return crashScreenShown ? EXIT_FAILURE : EXIT_SUCCESS;
}
