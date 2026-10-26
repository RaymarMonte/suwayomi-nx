#include "app/Logger.hpp"

#include <borealis.hpp>
#include <chrono>
#include <mutex>
#include <string>

#include "app/crash/Crash.hpp"
#include "core/LogBuffer.hpp"

namespace snx::app::logger {

namespace {

constexpr const char* kLogPath = "logs/app.log";
constexpr const char* kOldLogPath = "logs/app.1.log";

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

// The buffer is filled from any thread that logs (borealis logs from its own threads too) and emptied on the main thread.
struct State {
    std::mutex mutex;
    core::LogBuffer buffer{core::LogBuffer::Limits{}};
    const platform::SafeFs* fs = nullptr;  // nullptr: no file (not started, stopped, or no logs folder)
    std::uint64_t fileBytes = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

State& state() {
    static State s;
    return s;
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - state().start).count();
}

void write(bool force) {
    State& s = state();
    std::string text;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.fs == nullptr || (!force && !s.buffer.due(nowMs())) || s.buffer.empty()) {
            return;
        }
        text = s.buffer.take(nowMs());
    }
    if (s.buffer.shouldRotate(s.fileBytes, text.size())) {
        s.fs->renameFile(kLogPath, kOldLogPath);  // replaces the older one
        s.fileBytes = 0;
    }
    if (s.fs->append(kLogPath, text)) {
        s.fileBytes += text.size();
        return;
    }
    // On the Switch a file another program has open (the harness reading it) can't be written: try again next time.
    std::lock_guard<std::mutex> lock(s.mutex);
    s.buffer.giveBack(std::move(text));
}

}  // namespace

void start(const platform::SafeFs& fs, const core::RuntimeProfile& profile, bool debug) {
    if (debug) {
        brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
    }
    brls::Logger::setThreadSafeLogging(true);
    platform::SafeFs::setRefusalLogger(logRefusedPath);
    State& s = state();
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.buffer = core::LogBuffer(core::LogBuffer::Limits{profile.logFileBytes, profile.logFlushMs});
    }
    if (fs.makeDirs("logs")) {
        if (fs.exists(kLogPath)) {
            fs.renameFile(kLogPath, kOldLogPath);  // the last run's log stays readable
        }
        std::lock_guard<std::mutex> lock(s.mutex);
        s.fs = &fs;
    }
    brls::Logger::getLogEvent()->subscribe([](brls::Logger::TimePoint, brls::LogLevel level, std::string message) {
        const char letter = levelLetter(level);
        crash::pushLogLine(letter, message);
        State& st = state();
        std::lock_guard<std::mutex> lock(st.mutex);
        st.buffer.add(nowMs(), letter, message);
    });
}

void onFrame() {
    write(false);
}

void flush() {
    write(true);
}

void stop() {
    write(true);
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.fs = nullptr;
}

}  // namespace snx::app::logger
