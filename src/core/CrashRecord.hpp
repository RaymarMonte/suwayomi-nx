#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

// The pure part of crash containment (docs/SAFETY.md S1): the crash record's text format, a fixed-size buffer to build it in,
// and the in-memory log ring buffer. The crash handler (src/app/crash/) runs after the app has crashed, when allocating memory
// or taking locks may hang or crash again. So everything here that the handler calls is noexcept and never allocates.
// The format is described in src/app/crash/README.md; the harness (tests/e2e/lib/crashrecord.ts) and the next launch read it.
namespace snx::core::crashrecord {

inline constexpr std::string_view kHeader = "suwayomi-nx crash record 1";
inline constexpr std::string_view kFooter = "end of crash record";

// deko3d (borealis's GPU backend) aborts with results of this module (SAFETY.md S1). Those skip the crash screen.
inline constexpr std::uint32_t kGpuResultModule = 359;
constexpr std::uint32_t resultModule(std::uint32_t result) {
    return result & 0x1FF;
}
constexpr std::uint32_t resultDescription(std::uint32_t result) {
    return (result >> 9) & 0x1FFF;
}

// Text written into a buffer the caller owns. Never allocates, never fails: what doesn't fit is cut off (truncated() says so).
class FixedText {
  public:
    FixedText(char* buffer, std::size_t capacity) noexcept : buf_(buffer), cap_(capacity) {}

    FixedText& text(std::string_view s) noexcept;
    // Like text(), but control characters (newlines, escape codes) become spaces, so one value stays on one line.
    FixedText& clean(std::string_view s) noexcept;
    FixedText& dec(std::uint64_t value) noexcept;
    // "0x" and the hex digits, padded with zeros to at least `digits` digits.
    FixedText& hex(std::uint64_t value, int digits = 1) noexcept;
    // "BASE + 0x1a2b" for an address inside the app's own code and data (so tools/symbolize-crash can look it up),
    // otherwise the plain address.
    FixedText& address(std::uint64_t addr, std::uint64_t moduleBase, std::uint64_t moduleSize) noexcept;

    std::string_view view() const noexcept { return {buf_, len_}; }
    bool truncated() const noexcept { return truncated_; }

  private:
    char* buf_;
    std::size_t cap_;
    std::size_t len_ = 0;
    bool truncated_ = false;
};

// The last kLines log lines, kept in memory so a crash record can say what happened just before. Any thread may push; the
// crash handler reads it without a lock (a line being written at that moment may come out garbled, which is fine there).
class LogRing {
  public:
    static constexpr std::size_t kLines = 64;
    static constexpr std::size_t kLineBytes = 200;

    void push(std::string_view line) noexcept;
    // How many lines are kept (at most kLines), and line i of them, oldest first.
    std::size_t size() const noexcept;
    std::string_view lineAt(std::size_t i) const noexcept;

  private:
    struct Line {
        std::atomic<std::uint16_t> length{0};
        char text[kLineBytes] = {};
    };
    std::array<Line, kLines> lines_{};
    std::atomic<std::uint64_t> pushed_{0};
};

// Everything a crash record says. Plain views and numbers, so filling it in allocates nothing.
struct CrashInfo {
    std::string_view type;    // cpu_exception, abort, gpu_abort, terminate, main_loop_exception, worker_exception, watchdog
    std::string_view what;    // one line in plain words
    std::string_view thread;  // main, watchdog or other
    std::string_view version;
    std::string_view flavor;    // test or release
    std::string_view platform;  // switch or desktop
    std::string_view mode;      // application or applet
    std::string_view buildId;   // GNU build id of the .elf, hex (empty if unknown)
    std::uint64_t uptimeMs = 0;

    bool hasResult = false;  // aborts (diagAbortWithResult) carry a result code
    std::uint32_t result = 0;

    bool hasRegisters = false;  // CPU exceptions carry the registers
    std::uint64_t pc = 0, lr = 0, sp = 0, fp = 0, far = 0;
    std::uint32_t esr = 0;

    std::uint64_t moduleBase = 0;  // where the app's code starts in memory, and how big it is (0 = unknown)
    std::uint64_t moduleSize = 0;
    const std::uint64_t* stack = nullptr;  // return addresses, innermost first
    std::size_t stackCount = 0;
    const LogRing* log = nullptr;
};

// Writes the record into `out` and returns its length. Never allocates. A record cut short has no footer line.
std::size_t formatCrashRecord(const CrashInfo& info, char* out, std::size_t capacity) noexcept;

// What the next launch (and the harness) needs from a record. Allocates; never called inside the crash handler.
struct CrashSummary {
    std::string type;
    std::string what;
    std::string thread;
    std::string platform;
    std::string buildId;
    bool complete = false;  // the footer is there, so the whole record made it to the SD card
};
// Nothing if the text doesn't start like a crash record.
std::optional<CrashSummary> parseCrashRecord(std::string_view text);

// File names: logs/crash-<UTC date>-<UTC time>-<n>.txt, e.g. "crash-20261025-153000-1.txt" (n counts records within one launch).
std::string crashFileName(const std::tm& utc, int n);
bool isCrashFileName(std::string_view name);

}  // namespace snx::core::crashrecord
