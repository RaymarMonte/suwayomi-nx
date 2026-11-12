#include "app/TestHooks.hpp"

#ifdef SNX_TEST_HOOKS

#include <borealis.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>
#include <utility>

#include "app/crash/Drills.hpp"
#include "core/Json.hpp"
#include "core/Result.hpp"
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
std::int64_t gBusySinceMs = -1;  // when the app last became busy, for the "busy" event (WP-10)
bool gBusyReported = false;
constexpr std::int64_t kBusyReportMs = 5000;
bool gUiBusy = false;
std::int64_t gIoMs = 0;  // main-thread time spent on the SD card since takeIoMs() (WP-13b)

// The touch channel (WP-13).
constexpr const char* kCommandsDir = "test/commands";
constexpr std::size_t kMaxCommandBytes = 64 * 1024;
constexpr std::int64_t kCommandPollMs = 100;
constexpr int kMaxUnreadablePolls = 50;  // a command that still doesn't parse after ~5 s fails
constexpr std::size_t kMaxFrames = 4000;
constexpr std::size_t kMaxFingers = 10;
struct PlayedFinger {
    int id = 0;
    brls::Point at;
};
struct PlayedFrame {
    std::int64_t ms = 0;
    std::vector<PlayedFinger> fingers;
};
struct DownFinger {
    brls::TouchState touch;          // the finger's id, last position and the view it touched
    brls::Activity* activity = nullptr;  // the screen it touched: gone or covered, the finger counts for nothing
};
struct Playback {
    int id = 0;
    std::vector<PlayedFrame> frames;
    std::size_t next = 0;
    std::int64_t startMs = 0;
    std::vector<DownFinger> down;
};
int gNextCommand = 1;
int gUnreadablePolls = 0;
std::int64_t gLastCommandPollMs = 0;
std::optional<Playback> gPlayback;

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
    const std::int64_t started = nowMs();
    const bool written = gFs->append(kEventsPath, gPending);
    gIoMs += nowMs() - started;
    if (!written) {
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

void commandDone(int id, bool ok, const std::string& error) {
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["id"] = id;
    fields["ok"] = ok;
    if (!error.empty()) {
        fields["error"] = error;
    }
    append(core::testevents::line(nowMs(), "command", fields));
    gIdle.markBusy();
}

// The frames of a "touches" command, or an error in words.
std::optional<std::vector<PlayedFrame>> readFrames(const nlohmann::json& doc, std::string& error) {
    const nlohmann::json* frames = core::json::field(doc, "frames");
    if (frames == nullptr || !frames->is_array() || frames->size() > kMaxFrames) {
        error = "frames must be a list of at most " + std::to_string(kMaxFrames);
        return std::nullopt;
    }
    std::vector<PlayedFrame> out;
    for (const nlohmann::json& frame : *frames) {
        PlayedFrame played;
        const nlohmann::json* fingers = frame.is_object() ? core::json::field(frame, "fingers") : nullptr;
        const std::optional<std::int64_t> ms = frame.is_object() ? core::json::int64(frame, "ms") : std::nullopt;
        if (!ms || fingers == nullptr || !fingers->is_array() || fingers->size() > kMaxFingers) {
            error = "every frame needs ms and a list of at most " + std::to_string(kMaxFingers) + " fingers";
            return std::nullopt;
        }
        played.ms = *ms;
        for (const nlohmann::json& finger : *fingers) {
            const auto id = finger.is_object() ? core::json::int32(finger, "id") : std::nullopt;
            const auto x = finger.is_object() ? core::json::number(finger, "x") : std::nullopt;
            const auto y = finger.is_object() ? core::json::number(finger, "y") : std::nullopt;
            if (!id || !x || !y) {
                error = "every finger needs id, x and y";
                return std::nullopt;
            }
            played.fingers.push_back({*id, brls::Point{static_cast<float>(*x), static_cast<float>(*y)}});
        }
        out.push_back(std::move(played));
    }
    return out;
}

// Looks for the next command file. A file that doesn't parse yet is read again at the next poll.
void readCommand() {
    const std::string path = std::string(kCommandsDir) + "/" + std::to_string(gNextCommand) + ".json";
    if (!gFs->exists(path)) {
        return;
    }
    const int id = gNextCommand;
    const std::optional<std::string> text = gFs->readFile(path, kMaxCommandBytes);
    core::Result<nlohmann::json> parsed =
        text ? core::parseJson(*text) : core::Result<nlohmann::json>::fail(core::ErrorKind::Parse, "can't be read");
    if (!parsed.ok() || !parsed.value().is_object()) {
        if (++gUnreadablePolls < kMaxUnreadablePolls) {
            return;  // still being written, or busy
        }
        gUnreadablePolls = 0;
        gFs->removeFile(path);
        ++gNextCommand;
        commandDone(id, false, "the command file doesn't parse");
        return;
    }
    gUnreadablePolls = 0;
    gFs->removeFile(path);
    ++gNextCommand;
    const nlohmann::json& doc = parsed.value();
    std::string error;
    if (core::json::string(doc, "cmd").value_or("") != "touches") {
        commandDone(id, false, "unknown command");
        return;
    }
    std::optional<std::vector<PlayedFrame>> frames = readFrames(doc, error);
    if (!frames) {
        commandDone(id, false, error);
        return;
    }
    Playback playback;
    playback.id = id;
    playback.frames = std::move(*frames);
    playback.startMs = nowMs();
    gPlayback = std::move(playback);
}

// Hands one finger's touch to the recognizers of the view it touched, as borealis's own touch loop does.
void deliver(DownFinger& finger) {
    const std::vector<brls::Activity*> stack = brls::Application::getActivitiesStack();
    if (stack.empty() || stack.back() != finger.activity || finger.touch.view == nullptr) {
        finger.touch.view = nullptr;  // the screen is gone or covered
        return;
    }
    finger.touch.view->gestureRecognizerRequest(finger.touch, brls::MouseState(), finger.touch.view);
}

// One step of the touches being played: the next frame once it is due (then every finger in it is down, every other one
// lifts), else every finger that is down is touched again where it is.
void playTouches(std::int64_t now) {
    if (!gPlayback) {
        return;
    }
    Playback& p = *gPlayback;
    if (p.next < p.frames.size() && p.frames[p.next].ms <= now - p.startMs) {
        const PlayedFrame& frame = p.frames[p.next++];
        std::vector<DownFinger> down;
        for (const PlayedFinger& f : frame.fingers) {
            auto old = std::find_if(p.down.begin(), p.down.end(), [&](const DownFinger& d) { return d.touch.fingerId == f.id; });
            DownFinger finger;
            if (old != p.down.end()) {
                finger = *old;
                finger.touch.phase = brls::TouchPhase::STAY;
            } else {
                // A new finger: the same hit test as borealis's (the top screen's content view).
                const std::vector<brls::Activity*> stack = brls::Application::getActivitiesStack();
                finger.activity = stack.empty() ? nullptr : stack.back();
                finger.touch.fingerId = f.id;
                finger.touch.phase = brls::TouchPhase::START;
                finger.touch.view = finger.activity ? finger.activity->getContentView()->hitTest(f.at) : nullptr;
            }
            finger.touch.position = f.at;
            deliver(finger);
            down.push_back(finger);
        }
        for (DownFinger& old : p.down) {
            const bool still = std::any_of(frame.fingers.begin(), frame.fingers.end(),
                                           [&](const PlayedFinger& f) { return f.id == old.touch.fingerId; });
            if (!still) {
                old.touch.phase = brls::TouchPhase::END;
                deliver(old);
            }
        }
        p.down = std::move(down);
    } else if (p.next >= p.frames.size()) {
        // The last frame still had fingers down: they lift now.
        for (DownFinger& finger : p.down) {
            finger.touch.phase = brls::TouchPhase::END;
            deliver(finger);
        }
        p.down.clear();
    } else {
        for (DownFinger& finger : p.down) {
            finger.touch.phase = brls::TouchPhase::STAY;
            deliver(finger);
        }
    }
    if (p.next >= p.frames.size() && p.down.empty()) {
        const int id = p.id;
        gPlayback.reset();
        commandDone(id, true, "");
    }
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
    // The touch channel's commands (WP-13) count from 1 in every launch: what an earlier run left goes.
    fs.removeTree(kCommandsDir);
    fs.makeDirs(kCommandsDir);
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

bool diskCache() {
    return gConfig.diskCache;
}

float maxDecodeScale() {
    return gConfig.maxDecodeScale;
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
    // A screen's event says something changed, so the next quiet spell reports "idle" again (WP-10: on the console an
    // action's answer came within one frame, the app never looked busy, and a scenario waited for "idle" in vain).
    gIdle.markBusy();
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

void setUiBusy(bool busy) {
    gUiBusy = busy;
}

std::int64_t takeIoMs() {
    return std::exchange(gIoMs, 0);
}

void onFrame() {
    const std::int64_t now = nowMs();
    if (gFs != nullptr) {
        playTouches(now);
        if (!gPlayback && now - gLastCommandPollMs >= kCommandPollMs) {
            gLastCommandPollMs = now;
            readCommand();
            gIoMs += nowMs() - now;
        }
    }
    if (now - gLastMemMs >= kMemEventMs) {
        gLastMemMs = now;
        append(core::testevents::mem(now, platform::runtimeInfo()));
    }
    // borealis keeps every running animation (and other per-frame task) in this list until it ends. Pending network and
    // decode jobs count as busy too (WP-08): a screen that waits for an answer isn't idle yet.
    const std::size_t tickings = brls::Ticking::runningTickings.size();
    const bool jobs = gBusy && gBusy();
    const bool busy = tickings > 0 || jobs || gUiBusy || gPlayback.has_value();
    // Busy for a long time: say why, so a scenario that waits for "idle" in vain shows what kept the app busy (WP-10).
    if (!busy) {
        gBusySinceMs = -1;
        gBusyReported = false;
    } else if (gBusySinceMs < 0) {
        gBusySinceMs = now;
    } else if (!gBusyReported && now - gBusySinceMs >= kBusyReportMs) {
        gBusyReported = true;
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["ms"] = now - gBusySinceMs;
        fields["tickings"] = tickings;
        fields["jobs"] = jobs;
        append(core::testevents::line(now, "busy", fields));
    }
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
bool diskCache() {
    return true;
}
float maxDecodeScale() {
    return 0;
}
void watchNet(net::HttpClient&) {}
void watchImages(image::ImageLoader&) {}
void steadyScreenshots() {}
void watchApp(std::function<bool()>) {}
void setUiBusy(bool) {}
std::int64_t takeIoMs() {
    return 0;
}
void write(std::string_view, const nlohmann::ordered_json&) {}
void screen(std::string_view) {}
void screen(std::string_view, const nlohmann::ordered_json&) {}
void error(std::string_view, std::string_view, std::string_view) {}
void onFrame() {}
void finish() {}

}  // namespace snx::app::testhooks

#endif
