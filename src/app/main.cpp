// suwayomi-nx entry point. The order matters (SAFETY.md S1, S12, D21):
//   1. crash containment (WP-03b), first of all,
//   2. the logger (WP-08), so even the applet-mode guard's one line is kept,
//   3. the applet-mode guard (WP-08): in applet mode the app shows D21's text and leaves, before anything else runs,
//   4. config, borealis, the HTTP client (WP-05), the Suwayomi client (WP-06), the image loader (WP-07), all gathered in one
//      AppContext that every screen gets (WP-08), then the main screen and the main loop.

#include <borealis.hpp>
#include <cstdint>
#include <cstdlib>
#include <ctime>
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
#include "ui/downloads/DownloadStore.hpp"
#include "ui/downloads/ServerDownloads.hpp"
#include "ui/downloads/SwitchDownloads.hpp"
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
bool runOneFrame(snx::app::ConfigStore& config, snx::ui::WaitingSaves& saves) {
    if (!brls::Application::mainLoop()) {
        return false;
    }
    snx::app::testhooks::onFrame();
    const bool readerMoving = config.postponedThisFrame();  // read before config.onFrame() clears it
    config.onFrame();
    saves.onFrame(readerMoving);
    snx::app::logger::onFrame();
    return true;
}

// The waiting-save queue's hooks (WP-15b): the app's clocks, borealis's timers and main thread, the log, the test event.
snx::ui::WaitingSaves::Hooks waitingSaveHooks() {
    snx::ui::WaitingSaves::Hooks hooks;
    hooks.clockMs = snx::app::appClockMs;
    hooks.nowSeconds = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
    hooks.later = [](int ms, std::function<void()> fn) { brls::delay(ms, std::move(fn)); };
    hooks.post = [](std::function<void()> fn) { brls::sync(std::move(fn)); };
    hooks.log = [](bool warning, const std::string& line) {
        if (warning) {
            brls::Logger::warning("{}", line);
        } else {
            brls::Logger::info("{}", line);
        }
    };
    hooks.report = [](const char* what, const snx::core::WaitingSave& entry, const std::string& reason,
                      std::size_t waiting, bool held) {
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["what"] = what;
        fields["chapterId"] = entry.chapterId;
        fields["page"] = entry.page;
        fields["read"] = entry.read;
        if (!reason.empty()) {
            fields["reason"] = reason;
        }
        fields["waiting"] = waiting;
        if (held) {
            fields["held"] = true;  // WP-18b: a reader has the chapter open
        }
        snx::app::testhooks::write("progress_queue", fields);
    };
    return hooks;
}

// The server download queue's limits, from the profile (WP-17).
snx::ui::ServerDownloads::Limits serverDownloadLimits(const snx::core::RuntimeProfile& profile) {
    snx::ui::ServerDownloads::Limits limits;
    limits.pollMs = profile.serverDownloadPollMs;
    limits.startWaitMs = profile.serverDownloadStartWaitMs;
    limits.retryMs = profile.progressSyncMs;
    limits.retryMaxMs = profile.progressRetryMaxMs;
    limits.batchMax = profile.serverDownloadBatchMax;
    limits.checkMs = profile.serverDownloadCheckMs;
    return limits;
}

// Every server download action's answer: the log, and {"ev":"server_download_action",...} in test builds.
void reportDownloadAction(const snx::ui::ServerDownloads::Change& change) {
    if (!change.action) {
        return;
    }
    const snx::ui::ServerDownloads::ActionDone& done = *change.action;
    const char* name = snx::ui::ServerDownloads::actionName(done.action);
    if (done.ok) {
        brls::Logger::info("server downloads: {} ({} chapters) ok, {} ms", name, done.chapters, done.ms);
    } else {
        brls::Logger::warning("server downloads: {} ({} chapters) failed: {}", name, done.chapters,
                              done.error ? done.error->message : std::string());
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["action"] = name;
    fields["chapters"] = done.chapters;
    fields["ok"] = done.ok;
    if (done.error) {
        fields["error"] = snx::api::apiErrorKindName(done.error->kind);
    }
    fields["ms"] = done.ms;
    snx::app::testhooks::write("server_download_action", fields);
}

// Every Switch download event (WP-18): the log, and in test builds {"ev":"switch_download",...} for a chapter that finished,
// {"ev":"switch_download_action",...} for an action and {"ev":"switch_downloads",...} when what the tab shows changed.
void reportSwitchDownloads(const snx::ui::SwitchDownloads& downloads, const snx::ui::SwitchDownloads::Change& change,
                           std::string& reported) {
    namespace testhooks = snx::app::testhooks;
    if (change.finished) {
        const auto& done = *change.finished;
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["chapter"] = done.chapterId;
        fields["ok"] = done.ok;
        fields["pages"] = done.pages;
        fields["bytes"] = done.bytes;
        fields["ms"] = done.ms;
        if (!done.ok) {
            fields["error"] = done.error;
            brls::Logger::warning("switch downloads: chapter {} failed: {}", done.chapterId, done.error);
        }
        testhooks::write("switch_download", fields);
    }
    if (change.action) {
        const char* name = snx::ui::SwitchDownloads::actionName(*change.action);
        brls::Logger::info("switch downloads: {} ({} chapters) {}", name, change.actionChapters, change.actionOk ? "ok" : "failed");
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["action"] = name;
        fields["chapters"] = change.actionChapters;
        fields["ok"] = change.actionOk;
        testhooks::write("switch_download_action", fields);
    }
#ifndef SNX_TEST_HOOKS
    return;  // the state event is for tests only
#endif
    if (!change.state) {
        return;
    }
    const snx::ui::SwitchDownloads::Space space = downloads.space();
    int chapters = 0;
    int damaged = 0;
    const auto rows = downloads.downloadedManga();
    for (const auto& row : rows) {
        chapters += row.chapters;
        damaged += row.damagedChapters;
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["loaded"] = downloads.loaded();
    fields["view"] = snx::core::switchQueueViewName(downloads.view());
    fields["wait"] = snx::core::switchWaitName(downloads.wait());
    fields["queued"] = downloads.queue().entries.size();
    fields["failed"] = downloads.failedCount();
    fields["paused"] = downloads.queue().paused;
    fields["downloading"] = downloads.downloadingChapter();
    if (const int id = downloads.downloadingChapter(); id > 0) {
        const auto status = downloads.chapterState(id);
        fields["pagesDone"] = status.pagesDone;
        fields["pages"] = status.pages;
    }
    fields["manga"] = rows.size();
    fields["chapters"] = chapters;
    fields["damaged"] = damaged;
    fields["used"] = space.used;
    fields["limit"] = space.limit;
    fields["free"] = space.free ? nlohmann::ordered_json(*space.free) : nlohmann::ordered_json(nullptr);
    nlohmann::ordered_json ids = nlohmann::ordered_json::array();
    for (const auto& entry : downloads.queue().entries) {
        ids.push_back(entry.chapter.id);
    }
    fields["ids"] = ids;
    // WP-18c: the first 10 rows as the tab shows them (state, pages done of how many, the first page that failed).
    nlohmann::ordered_json rowList = nlohmann::ordered_json::array();
    for (const auto& entry : downloads.queue().entries) {
        if (rowList.size() >= 10) {
            break;
        }
        const auto status = downloads.chapterState(entry.chapter.id);
        rowList.push_back(nlohmann::ordered_json{{"chapter", entry.chapter.id},
                                                 {"state", snx::ui::SwitchDownloads::chapterStateName(status.state)},
                                                 {"done", status.pagesDone},
                                                 {"pages", status.pages},
                                                 {"failedPage", status.failedPage}});
    }
    fields["rows"] = rowList;
    const std::string key = fields.dump();
    if (key == reported) {
        return;
    }
    reported = key;
    fields["queueWrites"] = downloads.queueWrites();
    fields["freeMs"] = downloads.freeMs();
    testhooks::write("switch_downloads", fields);
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
    snx::core::RuntimeProfile profile =
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
    {
        // The console's time zone: Updates and History group their rows by the local day (WP-15).
        const std::int64_t offset = snx::platform::utcOffsetSeconds(static_cast<std::int64_t>(std::time(nullptr)));
        const std::int64_t minutes = (offset < 0 ? -offset : offset) / 60;
        brls::Logger::info("app: time zone UTC{}{:02}:{:02}", offset < 0 ? "-" : "+", minutes / 60, minutes % 60);
    }
    testhooks::start(fs);
    // Test builds only: the decode cap the console measurement asks for (WP-13c). Nothing reads the profile before this
    // except the logger, which doesn't use it.
    if (const float scale = testhooks::maxDecodeScale(); scale > 0) {
        profile.maxDecodeScale = scale;
        brls::Logger::info("app: test config: maxDecodeScale {}", scale);
    }
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
    clientOptions.sourceTimeoutMs = snx::core::sourceWaitMs(config.config().browse, profile.httpSourceTimeoutMs);  // D60
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

    // The waiting-save queue (WP-15b): progress saves that failed as the reader closed, from progress-queue.json. Any HTTP
    // answer from the server (GraphQL or an image) nudges it: the server is there again. The observer runs on the network
    // thread; serverAnswered() only posts to the main thread, and only while saves wait.
    snx::ui::WaitingSaves saves(client, &fs, snx::ui::WaitingSaves::optionsFrom(profile), waitingSaveHooks());
    saves.load();
    http.addResponseObserver([&saves](const snx::net::Request&, const snx::net::HttpResult& result) {
        if (result.ok()) {
            saves.serverAnswered();
        }
    });

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
    imageOptions.files = &fs;  // WP-18: downloaded pages and covers, read from the SD card
    snx::image::ImageLoader images(http, imageOptions);
    testhooks::watchImages(images);

    // GPU memory for the images on screen (S7): every texture a screen makes is reserved here first (WP-09).
    snx::image::TextureBudget textures(profile.textureBudgetBytes);

    // The server's download queue (WP-17, D58): one for the app, watched by the Server downloads tab and the details
    // screen. Every action's answer is logged and written as a test event here, wherever it was tapped.
    snx::ui::ServerDownloads downloads(client, serverDownloadLimits(profile), snx::app::appClockMs,
                                       [](int ms, std::function<void()> fn) { brls::delay(ms, std::move(fn)); });
    downloads.addListener(reportDownloadAction);

    // Downloads to this Switch (WP-18, D59): the files under downloads/ (one worker thread, SafeFs only) and the queue,
    // one chapter at a time at Background priority. It starts once the main screen's server check worked (connected()).
    snx::ui::DownloadStore::Options storeOptions = snx::ui::DownloadStore::optionsFrom(profile);
    if (const auto fakeFree = testhooks::sdFreeBytes()) {
        brls::Logger::info("app: test config: sdFreeBytes {}", *fakeFree);
        storeOptions.freeBytes = [free = *fakeFree] { return std::optional<std::uint64_t>(free); };
    }
    snx::ui::DownloadStore switchStore(fs, storeOptions, [](std::function<void()> task) { brls::sync(std::move(task)); },
                                       [](bool warning, const std::string& line) {
                                           brls::sync([warning, line] {
                                               if (warning) {
                                                   brls::Logger::warning("{}", line);
                                               } else {
                                                   brls::Logger::info("{}", line);
                                               }
                                           });
                                       });
    snx::ui::SwitchDownloads::Hooks switchHooks;
    switchHooks.clockMs = snx::app::appClockMs;
    switchHooks.later = [](int ms, std::function<void()> fn) { brls::delay(ms, std::move(fn)); };
    switchHooks.post = [](std::function<void()> fn) { brls::sync(std::move(fn)); };
    switchHooks.nowSeconds = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
    switchHooks.memoryLow = [&images] { return images.prefetchPaused(); };
    switchHooks.log = [](bool warning, const std::string& line) {
        if (warning) {
            brls::Logger::warning("{}", line);
        } else {
            brls::Logger::info("{}", line);
        }
    };
    snx::ui::SwitchDownloads switchDownloads(client, http, switchStore, snx::ui::SwitchDownloads::limitsFrom(profile),
                                             switchHooks);
    if (const auto limit = testhooks::switchDownloadLimitBytes()) {
        brls::Logger::info("app: test config: switchDownloadLimitBytes {}", *limit);
        switchDownloads.setLimit(*limit);
    } else {
        switchDownloads.setLimit(snx::core::switchLimitBytes(config.config().switchDownloads, profile.switchDownloadLimitBytes));
    }
    switchDownloads.setPageFormat(config.config().reader.serverConvertsToJpeg ? snx::api::PageFormat::Jpeg
                                                                              : snx::api::PageFormat::AsStored);
    std::string switchReported;
    switchDownloads.addListener([&switchDownloads, &switchReported](const snx::ui::SwitchDownloads::Change& change) {
        reportSwitchDownloads(switchDownloads, change, switchReported);
    });
    if (config.serverSetUp()) {
        switchDownloads.setServer(config.config().server.url);  // reads the SD card at once; downloads after connected()
    }

    app::AppContext context{fs,       runtime, profile, lastRun,   config,          http,
                            client,   images,  textures, saves,    downloads,       switchDownloads};
    // Test builds' idle rule: no network request or image load pending, and no animation (TESTING.md). Background requests
    // (the Switch downloads, WP-18c) don't count: they run for minutes while the screen has long settled.
    testhooks::watchApp([&http, &images] { return http.inFlightForeground() > 0 || images.activeJobs() > 0; });

    // HOME, sleep, another app on top: write what is waiting now (S5), and pause the watchdog (borealis keeps the loop
    // running in the background, but slowly). Waking up from sleep: fresh network connections.
    brls::Application::getWindowFocusChangedEvent()->subscribe([&config, &saves, &switchDownloads](bool focused) {
        crash::setFocused(focused);
        switchDownloads.setFocused(focused);  // S5: HOME / sleep: its requests stop, nothing is written, queue.json saved
        if (!focused) {
            brls::Logger::info("app: in the background, saving");
            config.flush();
            saves.flush();
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
            running = runOneFrame(config, saves);
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
    saves.flush();  // the waiting saves, written the safe way like config.json (S5)
    switchDownloads.shutdown();  // WP-18: its requests end, queue.json is written (S5)
    switchStore.stop();          // the file thread: the task under way ends, the rest are dropped
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
