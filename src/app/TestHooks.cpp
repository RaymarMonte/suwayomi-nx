#include "app/TestHooks.hpp"

#ifdef SNX_TEST_HOOKS

#include <sys/stat.h>

#include <borealis.hpp>
#include <chrono>
#include <cstdio>
#include <thread>

#include "core/TestEvents.hpp"
#include "core/Version.hpp"
#include "platform/Platform.hpp"

namespace snx::app::testhooks {

namespace {

std::string gPath;  // <data>/test/events.jsonl, empty until start() worked
std::chrono::steady_clock::time_point gStart;
core::testevents::IdleTracker gIdle;
std::string gPending;  // lines not written yet

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gStart).count();
}

// Appends the pending lines: open, write, close. The file is never left open, so sys-autopilot can read it while the app runs
// (the Switch harness polls it), and a crash or a "close software" from HOME loses at most what is still pending.
// On the Switch a file can't be opened for writing while another program reads it. Then the lines stay pending and the next
// frame tries again (found in WP-03: an "idle" event got lost that way).
// Stop-gap like the log file in main.cpp: one fixed file in our own folder. WP-03b moves it to SafeFs.
bool flush() {
    if (gPath.empty() || gPending.empty()) {
        return true;
    }
    std::FILE* file = std::fopen(gPath.c_str(), "a");
    if (file == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(gPending.data(), 1, gPending.size(), file) == gPending.size();
    std::fclose(file);
    if (ok) {
        gPending.clear();
    }
    return ok;
}

void append(const std::string& line) {
    if (gPath.empty()) {
        return;
    }
    gPending += line;
    flush();
}

}  // namespace

void start(const std::string& dataDir) {
    gStart = std::chrono::steady_clock::now();
    const std::string dir = dataDir + "test/";
    mkdir(dataDir.c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    const std::string path = dir + "events.jsonl";
    // "w" empties what an earlier run left behind. The harness also deletes the file before each launch.
    if (std::FILE* file = std::fopen(path.c_str(), "w")) {
        std::fclose(file);
        gPath = path;
    } else {
        brls::Logger::error("test hooks: can't write {}", path);
    }
}

void appStart(const core::RuntimeInfo& runtime, std::string_view profile) {
    core::testevents::AppStart info;
    info.version = core::appVersion();
    info.build = core::buildFlavor();
    info.platform = platform::platformName();
    info.runtime = runtime;
    info.profile = profile;
    append(core::testevents::appStart(nowMs(), info));
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
    } else {
        flush();  // retry lines that couldn't be written last time
    }
}

void finish() {
    // Last chance for pending lines: a few tries over about 100 ms (the harness reads the file every 250 ms at most).
    for (int i = 0; i < 10 && !flush(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace snx::app::testhooks

#else  // release build: no test events

namespace snx::app::testhooks {

void start(const std::string&) {}
void appStart(const core::RuntimeInfo&, std::string_view) {}
void screen(std::string_view) {}
void onFrame() {}
void finish() {}

}  // namespace snx::app::testhooks

#endif
