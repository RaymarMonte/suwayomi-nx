#include "core/ProgressQueue.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/StripMath.hpp"

namespace snx::core {

namespace {

// Cuts text to at most `max` bytes without splitting a UTF-8 character.
std::string cut(std::string text, std::size_t max) {
    if (text.size() <= max) {
        return text;
    }
    std::size_t end = max;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;  // text[end] continues a character: cut before that character starts
    }
    text.resize(end);
    return text;
}

// A spot as the note stores it: in ten-thousandths (4 decimals).
long spotKey(double spot) {
    return std::lround(std::clamp(spot, 0.0, 1.0) * 10000.0);
}

nlohmann::json baseToJson(const ServerProgress& base) {
    nlohmann::json json = nlohmann::json::object();
    json["page"] = base.page;
    json["read"] = base.read;
    json["lastReadAt"] = base.lastReadAt;
    return json;
}

// One entry from the file, or nothing (and why) when it can't be used.
std::optional<WaitingSave> entryFromJson(const nlohmann::json& node, std::string& why) {
    if (!node.is_object()) {
        why = "not an object";
        return std::nullopt;
    }
    WaitingSave entry;
    entry.serverUrl = json::string(node, "server").value_or("");
    entry.user = json::string(node, "user").value_or("");
    if (entry.serverUrl.empty() || entry.serverUrl.size() > kWaitingTextMaxBytes || entry.user.size() > kWaitingTextMaxBytes) {
        why = "no usable server";
        return std::nullopt;
    }
    const auto chapterId = json::int32(node, "chapterId");
    const auto page = json::int32(node, "page");
    if (!chapterId || *chapterId <= 0 || !page || *page < 0) {
        why = "no chapter id or page";
        return std::nullopt;
    }
    entry.chapterId = *chapterId;
    entry.page = *page;
    entry.mangaId = std::max(json::int32(node, "mangaId").value_or(0), 0);
    entry.read = json::boolean(node, "read").value_or(false);
    if (const auto spot = json::number(node, "spot"); spot && *spot >= 0.0 && *spot <= 1.0) {
        entry.spot = *spot;
    }
    if (const nlohmann::json* base = json::object(node, "base")) {
        entry.base.page = std::max(json::int32(*base, "page").value_or(0), 0);
        entry.base.read = json::boolean(*base, "read").value_or(false);
        entry.base.lastReadAt = std::max<std::int64_t>(json::int64(*base, "lastReadAt").value_or(0), 0);
    }
    entry.mangaTitle = cut(json::string(node, "manga").value_or(""), kWaitingTextMaxBytes);
    entry.chapterName = cut(json::string(node, "chapter").value_or(""), kWaitingTextMaxBytes);
    entry.madeAt = std::max<std::int64_t>(json::int64(node, "madeAt").value_or(0), 0);
    entry.errors = std::clamp(json::int32(node, "errors").value_or(0), 0, 100);
    return entry;
}

}  // namespace

bool sameChapter(const WaitingSave& a, const WaitingSave& b) {
    return a.chapterId == b.chapterId && a.serverUrl == b.serverUrl && a.user == b.user;
}

std::vector<WaitingSave> addWaitingSave(std::vector<WaitingSave>& entries, WaitingSave entry, std::size_t max) {
    const auto old = std::find_if(entries.begin(), entries.end(), [&](const WaitingSave& e) { return sameChapter(e, entry); });
    if (old != entries.end()) {
        entry.base = old->base;  // what the server had before the first of these saves
        entries.erase(old);
    }
    entry.mangaTitle = cut(std::move(entry.mangaTitle), kWaitingTextMaxBytes);
    entry.chapterName = cut(std::move(entry.chapterName), kWaitingTextMaxBytes);
    entries.push_back(std::move(entry));
    std::vector<WaitingSave> dropped;
    const std::size_t keep = std::max<std::size_t>(max, 1);
    while (entries.size() > keep) {
        dropped.push_back(std::move(entries.front()));
        entries.erase(entries.begin());
    }
    return dropped;
}

ProgressQueueLoad loadProgressQueue(std::string_view text, const JsonLimits& limits, std::size_t max) {
    ProgressQueueLoad load;
    Result<nlohmann::json> parsed = parseJson(text, limits);
    if (!parsed.ok()) {
        load.problems.push_back("progress queue: " + parsed.error().message);
        return load;
    }
    const nlohmann::json& root = parsed.value();
    const nlohmann::json* list = json::array(root, "entries");
    if (!list) {
        load.problems.push_back("progress queue: no \"entries\" list");
        return load;
    }
    load.usable = true;
    const int version = json::int32(root, "version").value_or(0);
    if (version != kProgressQueueVersion) {
        load.problems.push_back("progress queue: version " + std::to_string(version) + ", read as version " +
                                std::to_string(kProgressQueueVersion));
    }
    std::size_t index = 0;
    for (const nlohmann::json& node : *list) {
        std::string why;
        std::optional<WaitingSave> entry = entryFromJson(node, why);
        if (!entry) {
            load.problems.push_back("progress queue: entry " + std::to_string(index) + " skipped (" + why + ")");
        } else {
            // The app never writes a chapter twice (addWaitingSave merges them); a damaged file might: the later one stays.
            const auto old = std::find_if(load.entries.begin(), load.entries.end(),
                                          [&](const WaitingSave& e) { return sameChapter(e, *entry); });
            if (old != load.entries.end()) {
                load.problems.push_back("progress queue: entry " + std::to_string(index) + " repeats chapter " +
                                        std::to_string(entry->chapterId) + ", the later one is kept");
                load.entries.erase(old);
            }
            load.entries.push_back(std::move(*entry));
        }
        ++index;
    }
    const std::size_t keep = std::max<std::size_t>(max, 1);
    if (load.entries.size() > keep) {
        load.problems.push_back("progress queue: " + std::to_string(load.entries.size()) + " entries, only the newest " +
                                std::to_string(keep) + " are kept");
        load.entries.erase(load.entries.begin(),
                           load.entries.begin() + static_cast<std::ptrdiff_t>(load.entries.size() - keep));
    }
    return load;
}

std::vector<WaitingSave> fitProgressQueue(std::vector<WaitingSave>& entries, std::size_t maxBytes) {
    std::vector<WaitingSave> dropped;
    while (!entries.empty() && saveProgressQueue(entries).size() > maxBytes) {
        dropped.push_back(std::move(entries.front()));
        entries.erase(entries.begin());
    }
    return dropped;
}

std::string saveProgressQueue(const std::vector<WaitingSave>& entries) {
    // One entry per line: small, and still readable in a text editor.
    std::string text = "{\"version\": " + std::to_string(kProgressQueueVersion) + ", \"entries\": [";
    bool first = true;
    for (const WaitingSave& entry : entries) {
        nlohmann::json json = nlohmann::json::object();
        json["server"] = entry.serverUrl;
        json["user"] = entry.user;
        json["chapterId"] = entry.chapterId;
        json["mangaId"] = entry.mangaId;
        json["page"] = entry.page;
        json["read"] = entry.read;
        if (entry.spot) {
            json["spot"] = *entry.spot;
        }
        json["base"] = baseToJson(entry.base);
        json["manga"] = entry.mangaTitle;
        json["chapter"] = entry.chapterName;
        json["madeAt"] = entry.madeAt;
        json["errors"] = entry.errors;
        text += first ? "\n  " : ",\n  ";
        text += dumpJson(json);
        first = false;
    }
    text += entries.empty() ? "]}\n" : "\n]}\n";
    return text;
}

WaitingDecision decideWaitingSave(const WaitingSave& entry, const ServerProgress& server) {
    bool spotSame = true;
    if (entry.spot) {
        const std::optional<StripSpot> note = parseStripSpot(server.spotNote);
        spotSame = note && note->page == entry.page && spotKey(note->share) == spotKey(*entry.spot);
    }
    if (server.page == entry.page && (!entry.read || server.read) && spotSame) {
        return WaitingDecision::Already;
    }
    if (server.sameAs(entry.base)) {
        return WaitingDecision::Send;
    }
    return WaitingDecision::ServerMoved;
}

const char* waitingDecisionName(WaitingDecision decision) {
    switch (decision) {
        case WaitingDecision::Already:
            return "already";
        case WaitingDecision::Send:
            return "send";
        case WaitingDecision::ServerMoved:
            return "server moved";
    }
    return "?";
}

ReadProgress progressOf(const WaitingSave& entry) {
    ReadProgress progress;
    progress.chapterId = entry.chapterId;
    progress.lastPageRead = entry.page;
    progress.isRead = entry.read;
    progress.spot = entry.spot;
    return progress;
}

std::string describeServerProgress(const ServerProgress& progress) {
    return "page " + std::to_string(progress.page + 1) + (progress.read ? ", read" : ", unread") + ", stamped " +
           std::to_string(progress.lastReadAt);
}

std::string describeWaitingSave(const WaitingSave& entry) {
    std::string text = (entry.mangaTitle.empty() ? std::string("?") : entry.mangaTitle) + " / " +
                       (entry.chapterName.empty() ? std::string("?") : entry.chapterName) + " (chapter " +
                       std::to_string(entry.chapterId) + "): page " + std::to_string(entry.page + 1) +
                       (entry.read ? ", read" : ", unread");
    if (entry.spot) {
        text += ", spot " + std::to_string(spotKey(*entry.spot));
    }
    return text;
}

}  // namespace snx::core
