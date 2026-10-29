#include "app/TestHooks.hpp"

#ifdef SNX_TEST_HOOKS

#include <borealis.hpp>
#include <chrono>
#include <thread>
#include <utility>

#include "app/crash/Drills.hpp"
#include "core/TestConfig.hpp"
#include "core/TestEvents.hpp"
#include "core/Version.hpp"
#include "platform/Platform.hpp"

namespace snx::app::testhooks {

namespace {

constexpr const char* kEventsPath = "test/events.jsonl";
constexpr const char* kConfigPath = "test/config.json";
constexpr std::size_t kMaxConfigBytes = 64 * 1024;
constexpr std::int64_t kMemEventMs = 10000;  // TESTING.md: a "mem" event every 10 s in test builds

const platform::SafeFs* gFs = nullptr;  // set by start() once the test folder exists
std::chrono::steady_clock::time_point gStart;
core::testevents::IdleTracker gIdle;
std::string gPending;  // lines not written yet
core::testconfig::TestConfig gConfig;
std::function<bool()> gBusy;
bool gDrillStarted = false;
bool gCacheWritingReported = false;
std::int64_t gLastMemMs = 0;

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gStart).count();
}

// Appends the pending lines: open, write, close (SafeFs::append). The file is never left open, so sys-autopilot can read it
// while the app runs (the Switch harness polls it), and a crash or a "close software" from HOME loses at most what is still
// pending. On the Switch a file can't be opened for writing while another program reads it. Then the lines stay pending and
// the next frame tries again (found in WP-03: an "idle" event got lost that way).
bool flush() {
    if (gFs == nullptr || gPending.empty()) {
        return true;
    }
    if (!gFs->append(kEventsPath, gPending)) {
        return false;
    }
    gPending.clear();
    return true;
}

void append(const std::string& line) {
    if (gFs == nullptr) {
        return;
    }
    gPending += line;
    flush();
}

void runGuardDrill() {
    drills::run(core::testconfig::Drill::NullWrite, *gFs);
}

}  // namespace

void start(const platform::SafeFs& fs) {
    gStart = std::chrono::steady_clock::now();
    if (!fs.makeDirs("test")) {
        brls::Logger::error("test hooks: can't make the test folder in {}", fs.root());
        return;
    }
    // Empties what an earlier run left behind. The harness also deletes the file before each launch.
    if (fs.exists(kEventsPath)) {
        fs.removeFile(kEventsPath);
    }
    gFs = &fs;
    if (const auto text = fs.readFile(kConfigPath, kMaxConfigBytes)) {
        gConfig = core::testconfig::parse(*text);
        for (const std::string& problem : gConfig.problems) {
            brls::Logger::warning("{}", problem);
        }
    }
}

void appStart(const core::RuntimeInfo& runtime, const core::RuntimeProfile& profile, bool safeMode,
              std::string_view lastCrash) {
    core::testevents::AppStart info;
    info.version = core::appVersion();
    info.build = core::buildFlavor();
    info.platform = platform::platformName();
    info.runtime = runtime;
    info.profile = profile.name;
    info.safeMode = safeMode;
    info.lastCrash = lastCrash;
    info.imageBudget = profile.imageCacheBytes;
    info.coverBudget = profile.coverCacheBytes;
    info.textureBudget = profile.textureBudgetBytes;
    info.maxDecode = profile.maxDecodeBytes;
    info.httpConcurrency = profile.httpConcurrency;
    info.decodeThreads = profile.decodeThreads;
    append(core::testevents::appStart(nowMs(), info));
}

void launchRefused(const core::RuntimeInfo& runtime) {
    append(core::testevents::launchRefused(nowMs(), runtime));
}

void (*appletGuardDrill())() {
    if (gFs == nullptr || gConfig.drill != core::testconfig::Drill::NullWrite) {
        return nullptr;  // only the null-pointer drill runs inside the guard (SAFETY.md S12)
    }
    append(core::testevents::drill(nowMs(), core::testconfig::drillName(gConfig.drill), "start"));
    return runGuardDrill;
}

void flushNow() {
    for (int i = 0; i < 10 && !flush(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

std::string serverUrl() {
    return gConfig.serverUrl;
}

std::optional<core::ServerSettings> testServer() {
    if (gConfig.serverUrl.empty()) {
        return std::nullopt;
    }
    core::ServerSettings server;
    server.url = gConfig.serverUrl;
    server.auth = gConfig.serverAuth.value_or(core::AuthMode::None);
    server.username = gConfig.serverUsername;
    server.password = gConfig.serverPassword;
    return server;
}

bool showSetup() {
    return gConfig.showSetup;
}

bool diagnostics() {
    return gConfig.diagnostics;
}

int diagnosticsBurst() {
    return gConfig.diagnosticsBurst;
}

std::vector<std::string> imageBenchPaths() {
    return gConfig.benchPaths;
}

int imageBenchRepeat() {
    return gConfig.benchRepeat;
}

void watchNet(net::HttpClient& http) {
    http.addResponseObserver([](const net::Request& request, const net::HttpResult& result) {
        core::testevents::NetEvent event;
        event.method = request.method == net::Method::Post ? "POST" : "GET";
        event.url = request.url;
        if (result.ok()) {
            event.status = result.value().status;
            event.attempts = result.value().attempts;
        } else {
            event.error = core::errorKindName(result.error().kind);
        }
        // Network thread here; the events file belongs to the main thread.
        brls::sync([event] { append(core::testevents::net(nowMs(), event)); });
    });
}

void watchImages(image::ImageLoader& loader) {
    // Loader threads here; the events file belongs to the main thread.
    loader.setEventObserver([](const core::testevents::ImageEvent& event) {
        brls::sync([event] { append(core::testevents::image(nowMs(), event)); });
    });
    loader.setPressureObserver([](int usedPercent, const char* action) {
        const std::string what = action;
        brls::sync([usedPercent, what] { append(core::testevents::memPressure(nowMs(), usedPercent, what)); });
    });
}

void steadyScreenshots() {
    // borealis draws a glow in color2 that moves around the focus frame (color1), and freezes it wherever it was when the
    // input stopped, so two screenshots of the same screen differed (WP-09, the Switch smoke golden). With color2 = color1
    // the glow vanishes into the frame.
    for (brls::Theme* theme : {&brls::Theme::getLightTheme(), &brls::Theme::getDarkTheme()}) {
        theme->addColor("brls/highlight/color2", theme->getColor("brls/highlight/color1"));
    }
}

void watchApp(std::function<bool()> busy) {
    gBusy = std::move(busy);
    brls::Application::getGlobalFocusChangeEvent()->subscribe([](brls::View* view) {
        if (view == nullptr) {
            return;
        }
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["view"] = view->describe();
        append(core::testevents::line(nowMs(), "focus", fields));
    });
}

void write(std::string_view ev, const nlohmann::ordered_json& fields) {
    append(core::testevents::line(nowMs(), ev, fields));
}

void screen(std::string_view name) {
    append(core::testevents::screen(nowMs(), name));
    gIdle.markBusy();
}

void screen(std::string_view name, const nlohmann::ordered_json& params) {
    append(core::testevents::screen(nowMs(), name, params));
    gIdle.markBusy();
}

void error(std::string_view where, std::string_view kind, std::string_view message) {
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["where"] = std::string(where);
    fields["kind"] = std::string(kind);
    fields["msg"] = std::string(message);
    append(core::testevents::line(nowMs(), "error", fields));
}

void onFrame() {
    const std::int64_t now = nowMs();
    if (now - gLastMemMs >= kMemEventMs) {
        gLastMemMs = now;
        append(core::testevents::mem(now, platform::runtimeInfo()));
    }
    // borealis keeps every running animation (and other per-frame task) in this list until it ends. Pending network and
    // decode jobs count as busy too (WP-08): a screen that waits for an answer isn't idle yet.
    const bool busy = !brls::Ticking::runningTickings.empty() || (gBusy && gBusy());
    if (gIdle.onFrame(busy)) {
        append(core::testevents::idle(now));
        if (gConfig.drill != core::testconfig::Drill::None && !gDrillStarted) {
            gDrillStarted = true;
            append(core::testevents::drill(now, core::testconfig::drillName(gConfig.drill), "start"));
            drills::run(gConfig.drill, *gFs);  // main-thread drills don't come back from here
        }
    } else if (drills::cacheWriteInProgress() && !gCacheWritingReported) {
        gCacheWritingReported = true;
        append(core::testevents::drill(now, core::testconfig::drillName(gConfig.drill), "writing"));
    } else {
        flush();  // retry lines that couldn't be written last time
    }
}

void finish() {
    drills::stop();
    // Last chance for pending lines: a few tries over about 100 ms (the harness reads the file every 250 ms at most).
    flushNow();
}

}  // namespace snx::app::testhooks

#else  // release build: no test events

namespace snx::app::testhooks {

void start(const platform::SafeFs&) {}
void appStart(const core::RuntimeInfo&, const core::RuntimeProfile&, bool, std::string_view) {}
void launchRefused(const core::RuntimeInfo&) {}
void (*appletGuardDrill())() {
    return nullptr;
}
void flushNow() {}
std::string serverUrl() {
    return {};
}
std::optional<core::ServerSettings> testServer() {
    return std::nullopt;
}
bool showSetup() {
    return false;
}
bool diagnostics() {
    return false;
}
int diagnosticsBurst() {
    return 0;
}
std::vector<std::string> imageBenchPaths() {
    return {};
}
int imageBenchRepeat() {
    return 0;
}
void watchNet(net::HttpClient&) {}
void watchImages(image::ImageLoader&) {}
void steadyScreenshots() {}
void watchApp(std::function<bool()>) {}
void write(std::string_view, const nlohmann::ordered_json&) {}
void screen(std::string_view) {}
void screen(std::string_view, const nlohmann::ordered_json&) {}
void error(std::string_view, std::string_view, std::string_view) {}
void onFrame() {}
void finish() {}

}  // namespace snx::app::testhooks

#endif
