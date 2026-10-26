#include "core/LogBuffer.hpp"

#include <utility>

namespace snx::core {

std::string formatLogLine(std::int64_t ms, char level, std::string_view message) {
    std::string line = std::to_string(ms);
    line += ' ';
    line += level;
    line += ' ';
    for (std::size_t i = 0; i < message.size(); ++i) {
        const char c = message[i];
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < message.size() && message[i + 1] == '\n') {
                ++i;  // \r\n is one break
            }
            line += " | ";
        } else {
            line += c;
        }
    }
    line += '\n';
    return line;
}

LogBuffer::LogBuffer(Limits limits) : limits_(limits) {}

void LogBuffer::add(std::int64_t ms, char level, std::string_view message) {
    std::string line = formatLogLine(ms, level, message);
    if (pending_.size() + line.size() > limits_.fileBytes / 4) {
        ++dropped_;
        return;
    }
    pending_ += line;
}

bool LogBuffer::due(std::int64_t nowMs) const {
    return !pending_.empty() && nowMs - lastTakeMs_ >= limits_.flushMs;
}

std::string LogBuffer::take(std::int64_t nowMs) {
    lastTakeMs_ = nowMs;
    std::string text;
    text.swap(pending_);
    return text;
}

void LogBuffer::giveBack(std::string text) {
    if (text.size() + pending_.size() > limits_.fileBytes / 4) {
        ++dropped_;  // the older text goes; what came since stays
        return;
    }
    pending_ = std::move(text) + pending_;
}

bool LogBuffer::shouldRotate(std::uint64_t fileSize, std::uint64_t adding) const {
    return fileSize > 0 && fileSize + adding > limits_.fileBytes;
}

}  // namespace snx::core
