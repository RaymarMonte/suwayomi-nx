#include "app/TestHooks.hpp"

#ifdef SNX_TEST_HOOKS

#include <borealis.hpp>
#include <chrono>
#include <thread>

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

const platform::SafeFs* gFs = nullptr;  // set by start() once the test folder exists
std::chrono::steady_clock::time_point gStart;
core::testevents::IdleTracker gIdle;
std::string gPending;  // lines not written yet
core::testconfig::Drill gDrill = core::testconfig::Drill::None;
std::string gServerUrl;
std::vector<std::string> gBenchPaths;
int gBenchRepeat = 0;
bool gDrillStarted = false;
bool gCacheWritingReported = false;

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
        const core::testconfig::TestConfig config = core::testconfig::parse(*text);
        for (const std::string& problem : config.problems) {
            brls::Logger::warning("{}", problem);
        }
        gDrill = config.drill;
        gServerUrl = config.serverUrl;
        gBenchPaths = config.benchPaths;
        gBenchRepeat = config.benchRepeat;
    }
}

std::vector<std::string> imageBenchPaths() {
    return gBenchPaths;
}

int imageBenchRepeat() {
    return gBenchRepeat;
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

void write(std::string_view ev, const nlohmann::ordered_json& fields) {
    append(core::testevents::line(nowMs(), ev, fields));
}

void appStart(const core::RuntimeInfo& runtime, std::string_view profile, bool safeMode, std::string_view lastCrash) {
    core::testevents::AppStart info;
    info.version = core::appVersion();
    info.build = core::buildFlavor();
    info.platform = platform::platformName();
    info.runtime = runtime;
    info.profile = profile;
    info.safeMode = safeMode;
    info.lastCrash = lastCrash;
    append(core::testevents::appStart(nowMs(), info));
}

std::string serverUrl() {
    return gServerUrl;
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

void screen(std::string_view name) {
    append(core::testevents::screen(nowMs(), name));
    gIdle.markBusy();
}

void onFrame() {
    // borealis keeps every running animation (and other per-frame task) in this list until it ends.
    const bool busy = !brls::Ticking::runningTickings.empty();
    if (gIdle.onFrame(busy)) {
        append(core::testevents::idle(nowMs()));
        if (gDrill != core::testconfig::Drill::None && !gDrillStarted) {
            gDrillStarted = true;
            append(core::testevents::drill(nowMs(), core::testconfig::drillName(gDrill), "start"));
            drills::run(gDrill, *gFs);  // main-thread drills don't come back from here
        }
    } else if (drills::cacheWriteInProgress() && !gCacheWritingReported) {
        gCacheWritingReported = true;
        append(core::testevents::drill(nowMs(), core::testconfig::drillName(gDrill), "writing"));
    } else {
        flush();  // retry lines that couldn't be written last time
    }
}

void finish() {
    drills::stop();
    // Last chance for pending lines: a few tries over about 100 ms (the harness reads the file every 250 ms at most).
    for (int i = 0; i < 10 && !flush(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace snx::app::testhooks

#else  // release build: no test events

namespace snx::app::testhooks {

void start(const platform::SafeFs&) {}
std::string serverUrl() {
    return {};
}
void watchNet(net::HttpClient&) {}
void watchImages(image::ImageLoader&) {}
std::vector<std::string> imageBenchPaths() {
    return {};
}
int imageBenchRepeat() {
    return 0;
}
void write(std::string_view, const nlohmann::ordered_json&) {}
void appStart(const core::RuntimeInfo&, std::string_view, bool, std::string_view) {}
void screen(std::string_view) {}
void onFrame() {}
void finish() {}

}  // namespace snx::app::testhooks

#endif
