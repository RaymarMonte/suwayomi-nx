#include "core/CrashRecord.hpp"

#include <cstdio>

namespace snx::core::crashrecord {

FixedText& FixedText::text(std::string_view s) noexcept {
    for (const char c : s) {
        if (len_ >= cap_) {
            truncated_ = true;
            break;
        }
        buf_[len_++] = c;
    }
    return *this;
}

FixedText& FixedText::clean(std::string_view s) noexcept {
    for (const char c : s) {
        const auto byte = static_cast<unsigned char>(c);
        const char out = (byte < 0x20 || byte == 0x7f) ? ' ' : c;
        text(std::string_view(&out, 1));
    }
    return *this;
}

FixedText& FixedText::dec(std::uint64_t value) noexcept {
    char digits[20];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (n > 0) {
        text(std::string_view(&digits[--n], 1));
    }
    return *this;
}

FixedText& FixedText::hex(std::uint64_t value, int digits) noexcept {
    static constexpr char kDigits[] = "0123456789abcdef";
    char out[16];
    int n = 0;
    do {
        out[n++] = kDigits[value & 0xF];
        value >>= 4;
    } while (value != 0);
    while (n < digits && n < 16) {
        out[n++] = '0';
    }
    text("0x");
    while (n > 0) {
        text(std::string_view(&out[--n], 1));
    }
    return *this;
}

FixedText& FixedText::address(std::uint64_t addr, std::uint64_t moduleBase, std::uint64_t moduleSize) noexcept {
    if (moduleSize != 0 && addr >= moduleBase && addr - moduleBase < moduleSize) {
        return text("BASE + ").hex(addr - moduleBase);
    }
    return hex(addr, 16);
}

void LogRing::push(std::string_view line) noexcept {
    const std::uint64_t index = pushed_.fetch_add(1, std::memory_order_relaxed);
    Line& slot = lines_[index % kLines];
    const std::size_t n = line.size() < kLineBytes ? line.size() : kLineBytes;
    slot.length.store(0, std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; i++) {
        slot.text[i] = line[i];
    }
    slot.length.store(static_cast<std::uint16_t>(n), std::memory_order_release);
}

std::size_t LogRing::size() const noexcept {
    const std::uint64_t pushed = pushed_.load(std::memory_order_acquire);
    return pushed < kLines ? static_cast<std::size_t>(pushed) : kLines;
}

std::string_view LogRing::lineAt(std::size_t i) const noexcept {
    const std::uint64_t pushed = pushed_.load(std::memory_order_acquire);
    const std::size_t kept = pushed < kLines ? static_cast<std::size_t>(pushed) : kLines;
    if (i >= kept) {
        return {};
    }
    const Line& slot = lines_[(pushed - kept + i) % kLines];
    std::size_t n = slot.length.load(std::memory_order_acquire);
    if (n > kLineBytes) {
        n = kLineBytes;
    }
    return {slot.text, n};
}

std::size_t formatCrashRecord(const CrashInfo& info, char* out, std::size_t capacity) noexcept {
    FixedText t(out, capacity);
    auto line = [&t](std::string_view key, std::string_view value) { t.text(key).text(": ").clean(value).text("\n"); };

    t.text(kHeader).text("\n");
    line("type", info.type);
    line("what", info.what);
    line("thread", info.thread);
    t.text("version: ").clean(info.version).text(" (").clean(info.flavor).text(" build)\n");
    line("platform", info.platform);
    line("mode", info.mode);
    line("build_id", info.buildId.empty() ? std::string_view("unknown") : info.buildId);
    t.text("uptime_ms: ").dec(info.uptimeMs).text("\n");
    if (info.hasResult) {
        t.text("result: ").hex(info.result, 8).text(" (module ").dec(resultModule(info.result));
        t.text(", description ").dec(resultDescription(info.result)).text(")\n");
    }
    if (info.moduleSize != 0) {
        t.text("module: base ").hex(info.moduleBase, 16).text(" size ").hex(info.moduleSize).text("\n");
    }
    if (info.hasRegisters) {
        t.text("registers:\n");
        t.text("  pc: ").address(info.pc, info.moduleBase, info.moduleSize).text("\n");
        t.text("  lr: ").address(info.lr, info.moduleBase, info.moduleSize).text("\n");
        t.text("  sp: ").hex(info.sp, 16).text("\n");
        t.text("  fp: ").hex(info.fp, 16).text("\n");
        t.text("  far: ").hex(info.far, 16).text("\n");
        t.text("  esr: ").hex(info.esr, 8).text("\n");
    }
    if (info.stackCount > 0) {
        t.text("stack (return addresses, innermost first):\n");
        for (std::size_t i = 0; i < info.stackCount; i++) {
            t.text("  #").dec(i).text(" ").address(info.stack[i], info.moduleBase, info.moduleSize).text("\n");
        }
    }
    if (info.log != nullptr) {
        t.text("log (last ").dec(info.log->size()).text(" lines, oldest first):\n");
        for (std::size_t i = 0; i < info.log->size(); i++) {
            t.text("  ").clean(info.log->lineAt(i)).text("\n");
        }
    }
    // The footer only goes in when everything above fitted, so "complete" really means complete.
    if (capacity - t.view().size() > kFooter.size() + 1 && !t.truncated()) {
        t.text(kFooter).text("\n");
    }
    return t.view().size();
}

std::optional<CrashSummary> parseCrashRecord(std::string_view text) {
    if (text.substr(0, kHeader.size()) != kHeader) {
        return std::nullopt;
    }
    CrashSummary summary;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        start = end + 1;
        if (line == kFooter) {
            summary.complete = true;
            continue;
        }
        const std::size_t colon = line.find(": ");
        if (colon == std::string_view::npos || line.front() == ' ') {
            continue;
        }
        const std::string_view key = line.substr(0, colon);
        const std::string value(line.substr(colon + 2));
        if (key == "type") {
            summary.type = value;
        } else if (key == "what") {
            summary.what = value;
        } else if (key == "thread") {
            summary.thread = value;
        } else if (key == "platform") {
            summary.platform = value;
        } else if (key == "build_id") {
            summary.buildId = value;
        }
    }
    return summary;
}

std::string crashFileName(const std::tm& utc, int n) {
    char name[64];
    std::snprintf(name, sizeof(name), "crash-%04d%02d%02d-%02d%02d%02d-%d.txt", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec, n);
    return name;
}

bool isCrashFileName(std::string_view name) {
    constexpr std::string_view kPrefix = "crash-";
    constexpr std::string_view kSuffix = ".txt";
    return name.size() > kPrefix.size() + kSuffix.size() && name.substr(0, kPrefix.size()) == kPrefix &&
           name.substr(name.size() - kSuffix.size()) == kSuffix;
}

}  // namespace snx::core::crashrecord
