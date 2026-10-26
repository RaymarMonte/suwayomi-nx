#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The pure part of the app's log file (WP-08): how a line looks, and when the buffered lines go to the SD card and when the
// file moves aside. The app's Logger (src/app/Logger) does the writing through SafeFs and calls these. Not thread-safe:
// the Logger owns one and uses it on the main thread.
namespace snx::core {

// "1234 I setup: testing http://192.168.1.10:4567\n": milliseconds since start, the level letter (E W I D V), the message.
// Line breaks inside the message become " | ", so one entry is always one line. The same format as the crash record's log.
std::string formatLogLine(std::int64_t ms, char level, std::string_view message);

class LogBuffer {
  public:
    struct Limits {
        std::uint64_t fileBytes = 1024 * 1024;  // RuntimeProfile::logFileBytes: a file moves aside before it grows past this
        int flushMs = 2000;                     // RuntimeProfile::logFlushMs: lines wait at most this long
    };

    explicit LogBuffer(Limits limits);

    // A new line. If writing keeps failing (no SD card), the waiting text stops growing at a quarter of fileBytes: newer
    // lines are then dropped and counted.
    void add(std::int64_t ms, char level, std::string_view message);

    // True when there is text waiting and flushMs has passed since the last take() (or since the start).
    bool due(std::int64_t nowMs) const;

    // The waiting text, now to be written. Afterwards nothing waits and the flushMs wait starts again.
    std::string take(std::int64_t nowMs);
    // Puts text back in front when writing it failed (it then goes with the next try).
    void giveBack(std::string text);

    // Before `adding` bytes go to a file that already has `fileSize`: should the file move aside (app.log -> app.1.log) first?
    // Never for an empty file, so one big write still lands.
    bool shouldRotate(std::uint64_t fileSize, std::uint64_t adding) const;

    bool empty() const { return pending_.empty(); }
    std::size_t droppedLines() const { return dropped_; }

  private:
    Limits limits_;
    std::string pending_;
    std::int64_t lastTakeMs_ = 0;
    std::size_t dropped_ = 0;
};

}  // namespace snx::core
