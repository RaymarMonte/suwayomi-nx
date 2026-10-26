// Small helpers that belong to the model structs (status names, server versions). The JSON parsers are in FromJson.cpp.
#include <array>
#include <cstddef>
#include <utility>

#include "core/models/Manga.hpp"
#include "core/models/ServerInfo.hpp"

namespace snx::core {

namespace {

constexpr std::array<std::pair<MangaStatus, std::string_view>, 7> kStatusNames = {{
    {MangaStatus::Unknown, "UNKNOWN"},
    {MangaStatus::Ongoing, "ONGOING"},
    {MangaStatus::Completed, "COMPLETED"},
    {MangaStatus::Licensed, "LICENSED"},
    {MangaStatus::PublishingFinished, "PUBLISHING_FINISHED"},
    {MangaStatus::Cancelled, "CANCELLED"},
    {MangaStatus::OnHiatus, "ON_HIATUS"},
}};

// Reads digits at text[i...] into `out`. False if there are none or the number gets absurdly big.
bool readNumber(std::string_view text, std::size_t& i, int& out) {
    const std::size_t start = i;
    long value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (text[i] - '0');
        if (value > 1'000'000'000) {
            return false;
        }
        ++i;
    }
    out = static_cast<int>(value);
    return i > start;
}

}  // namespace

MangaStatus mangaStatusFromName(std::string_view name) {
    for (const auto& [status, text] : kStatusNames) {
        if (text == name) {
            return status;
        }
    }
    return MangaStatus::Unknown;
}

std::string_view mangaStatusName(MangaStatus status) {
    for (const auto& [s, text] : kStatusNames) {
        if (s == status) {
            return text;
        }
    }
    return "UNKNOWN";
}

ServerVersion parseServerVersion(std::string_view text) {
    if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
        text = text.substr(0, dash);
    }
    if (!text.empty() && (text[0] == 'v' || text[0] == 'V')) {
        text.remove_prefix(1);
    }
    ServerVersion version;
    std::size_t i = 0;
    if (!readNumber(text, i, version.major) || i >= text.size() || text[i] != '.') {
        return {};
    }
    ++i;
    if (!readNumber(text, i, version.minor)) {
        return {};
    }
    if (i < text.size()) {
        if (text[i] != '.') {
            return {};
        }
        ++i;
        if (!readNumber(text, i, version.patch) || i != text.size()) {
            return {};
        }
    }
    version.known = true;
    return version;
}

bool versionAtLeast(const ServerVersion& a, const ServerVersion& b) {
    if (a.major != b.major) {
        return a.major > b.major;
    }
    if (a.minor != b.minor) {
        return a.minor > b.minor;
    }
    return a.patch >= b.patch;
}

bool isSupportedServer(const ServerInfo& info) {
    const ServerVersion version = parseServerVersion(info.version);
    return version.known && versionAtLeast(version, kMinimumServerVersion);
}

}  // namespace snx::core
