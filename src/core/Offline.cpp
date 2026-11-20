#include "core/Offline.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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
        --end;
    }
    text.resize(end);
    return text;
}

// Days from 1970-01-01 to the given day of the proleptic Gregorian calendar (Howard Hinnant's days_from_civil).
std::int64_t daysFromCivil(std::int64_t year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yearOfEra = static_cast<std::int64_t>(year - era * 400);
    const std::int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const std::int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

bool leapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
    static constexpr std::array<int, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && leapYear(year) ? 29 : kDays[static_cast<std::size_t>(month - 1)];
}

// `count` digits at text[at...] as a number, or -1.
int digits(std::string_view text, std::size_t at, std::size_t count) {
    if (at + count > text.size()) {
        return -1;
    }
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char c = text[at + i];
        if (c < '0' || c > '9') {
            return -1;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

nlohmann::json localToJson(const LocalProgress& local) {
    nlohmann::json json = nlohmann::json::object();
    json["page"] = local.page;
    json["read"] = local.read;
    if (local.spot) {
        json["spot"] = *local.spot;
    }
    json["at"] = local.at;
    return json;
}

std::string entryLine(int chapterId, const KnownProgress& known) {
    nlohmann::json json = nlohmann::json::object();
    json["id"] = chapterId;
    json["page"] = known.server.page;
    json["read"] = known.server.read;
    json["lastReadAt"] = known.server.lastReadAt;
    if (known.server.bookmarked) {
        json["bookmarked"] = true;
    }
    if (!known.server.spotNote.empty()) {
        json["spotNote"] = known.server.spotNote;
    }
    if (known.gone) {
        json["gone"] = true;
    }
    if (known.local) {
        json["local"] = localToJson(*known.local);
    }
    return dumpJson(json);
}

std::optional<double> spotFrom(const nlohmann::json& node) {
    if (const auto spot = json::number(node, "spot"); spot && *spot >= 0.0 && *spot <= 1.0) {
        return *spot;
    }
    return std::nullopt;
}

constexpr const char* kFileStart = "{\"version\": 1, \"chapters\": [";

}  // namespace

std::optional<std::int64_t> parseHttpDate(std::string_view text) {
    // "Fri, 20 Nov 2026 05:03:18 GMT": exactly 29 bytes. Leading and trailing spaces are allowed (header values).
    if (text.size() > kHttpDateMaxBytes) {
        return std::nullopt;
    }
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    if (text.size() != 29 || text[3] != ',' || text[4] != ' ' || text[7] != ' ' || text[11] != ' ' || text[16] != ' ' ||
        text[19] != ':' || text[22] != ':' || text[25] != ' ' || text.substr(26) != "GMT") {
        return std::nullopt;
    }
    static constexpr std::array<std::string_view, 7> kDays = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static constexpr std::array<std::string_view, 12> kMonths = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (std::find(kDays.begin(), kDays.end(), text.substr(0, 3)) == kDays.end()) {
        return std::nullopt;
    }
    const auto month = std::find(kMonths.begin(), kMonths.end(), text.substr(8, 3));
    if (month == kMonths.end()) {
        return std::nullopt;
    }
    const int monthNumber = static_cast<int>(month - kMonths.begin()) + 1;
    const int day = digits(text, 5, 2);
    const int year = digits(text, 12, 4);
    const int hour = digits(text, 17, 2);
    const int minute = digits(text, 20, 2);
    const int second = digits(text, 23, 2);
    if (year < 1970 || day < 1 || day > daysInMonth(year, monthNumber) || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 60) {
        return std::nullopt;
    }
    const std::int64_t days = daysFromCivil(year, monthNumber, day);
    return days * 86400 + hour * 3600 + minute * 60 + std::min(second, 59);  // a leap second counts as :59
}

bool KnownProgress::operator==(const KnownProgress& other) const {
    return server.sameAs(other.server) && server.bookmarked == other.server.bookmarked &&
           server.spotNote == other.server.spotNote && gone == other.gone && local == other.local;
}

std::string writeProgressFile(const SwitchProgress& progress) {
    std::string text = kFileStart;
    bool first = true;
    for (const auto& [chapterId, known] : progress) {
        text += first ? "\n  " : ",\n  ";
        text += entryLine(chapterId, known);
        first = false;
    }
    text += progress.empty() ? "]}\n" : "\n]}\n";
    return text;
}

ProgressFileLoad readProgressFile(std::string_view text, const JsonLimits& limits) {
    ProgressFileLoad load;
    Result<nlohmann::json> parsed = parseJson(text, limits);
    if (!parsed.ok()) {
        load.problems.push_back("progress.json: " + parsed.error().message);
        return load;
    }
    const nlohmann::json& root = parsed.value();
    const nlohmann::json* list = json::array(root, "chapters");
    if (!list) {
        load.problems.push_back("progress.json: no \"chapters\" list");
        return load;
    }
    load.usable = true;
    const int version = json::int32(root, "version").value_or(0);
    if (version != kSwitchFileVersion) {
        load.problems.push_back("progress.json: version " + std::to_string(version) + ", read as version " +
                                std::to_string(kSwitchFileVersion));
    }
    for (const nlohmann::json& node : *list) {
        const int id = node.is_object() ? json::int32(node, "id").value_or(0) : 0;
        const auto page = node.is_object() ? json::int32(node, "page") : std::nullopt;
        if (id <= 0 || !page || *page < 0) {
            ++load.skipped;
            continue;
        }
        KnownProgress known;
        known.server.page = *page;
        known.server.read = json::boolean(node, "read").value_or(false);
        known.server.lastReadAt = std::max<std::int64_t>(json::int64(node, "lastReadAt").value_or(0), 0);
        known.server.bookmarked = json::boolean(node, "bookmarked").value_or(false);
        known.server.spotNote = cut(json::string(node, "spotNote").value_or(""), kSpotNoteMaxBytes);
        known.gone = json::boolean(node, "gone").value_or(false);
        if (const nlohmann::json* local = json::object(node, "local")) {
            LocalProgress l;
            l.page = std::max(json::int32(*local, "page").value_or(0), 0);
            l.read = json::boolean(*local, "read").value_or(false);
            l.spot = spotFrom(*local);
            l.at = std::max<std::int64_t>(json::int64(*local, "at").value_or(0), 0);
            known.local = l;
        }
        load.progress[id] = std::move(known);  // a repeated chapter: the later one stays
    }
    if (load.skipped > 0) {
        load.problems.push_back("progress.json: " + std::to_string(load.skipped) + " broken entries skipped");
    }
    return load;
}

int fitProgressFile(SwitchProgress& progress, std::size_t maxBytes) {
    // writeProgressFile's size, exactly: the start, "\n  " before the first line and ",\n  " before each other one, then
    // "\n]}\n" (only "]}\n" without lines).
    const std::size_t start = std::string(kFileStart).size();
    std::vector<std::pair<int, std::size_t>> lines;
    lines.reserve(progress.size());
    std::size_t sum = 0;
    for (const auto& [chapterId, known] : progress) {
        const std::size_t line = entryLine(chapterId, known).size();
        lines.emplace_back(chapterId, line);
        sum += line;
    }
    auto size = [&] { return lines.empty() ? start + 3 : start + sum + 4 * lines.size() + 3; };
    int dropped = 0;
    while (size() > maxBytes && !lines.empty()) {
        sum -= lines.back().second;
        progress.erase(lines.back().first);
        lines.pop_back();
        ++dropped;
    }
    return dropped;
}

Chapter offlineChapter(const SwitchChapter& downloaded, int mangaId, const KnownProgress* known, const WaitingSave* waiting,
                       std::int64_t clockOffsetSeconds) {
    Chapter chapter = chapterFrom(downloaded, mangaId);
    if (known != nullptr) {
        chapter.lastPageRead = known->server.page;
        chapter.isRead = known->server.read;
        chapter.lastReadAt = known->server.lastReadAt;
        chapter.isBookmarked = known->server.bookmarked;
        chapter.stripSpotNote = known->server.spotNote;
        if (known->gone) {
            chapter.goneFromServer = true;
            if (known->local) {
                const LocalProgress& local = *known->local;
                chapter.lastPageRead = local.page;
                chapter.isRead = chapter.isRead || local.read;
                chapter.stripSpotNote = local.spot ? stripSpotValue(local.page, *local.spot) : std::string();
                if (local.at > 0) {
                    chapter.lastReadAt = std::max(chapter.lastReadAt, local.at + clockOffsetSeconds);
                }
            }
        }
    }
    if (waiting != nullptr) {
        chapter.lastPageRead = waiting->page;
        chapter.isRead = chapter.isRead || waiting->read;  // a waiting save never un-reads (D44)
        if (waiting->spot) {
            chapter.stripSpotNote = stripSpotValue(waiting->page, *waiting->spot);
        }
        if (waiting->madeAt > 0) {
            chapter.lastReadAt = std::max(chapter.lastReadAt, waiting->madeAt + clockOffsetSeconds);
        }
    }
    if (chapter.pageCount > 0) {
        chapter.lastPageRead = std::clamp(chapter.lastPageRead, 0, chapter.pageCount - 1);
    }
    return chapter;
}

Manga mangaFrom(const SwitchManga& manga) {
    Manga out;
    out.id = manga.id;
    out.title = manga.title;
    out.thumbnailUrl = manga.thumbnailUrl;
    out.author = manga.author;
    out.artist = manga.artist;
    out.description = manga.description;
    out.genre = manga.genres;
    out.status = mangaStatusFromName(manga.status);
    out.inLibrary = true;
    if (!manga.sourceId.empty() || !manga.sourceName.empty()) {
        Source source;
        source.id = manga.sourceId;
        source.name = manga.sourceName;
        source.displayName = manga.sourceName;
        out.source = std::move(source);
    }
    return out;
}

Manga offlineManga(const SwitchManga& manga, const std::vector<Chapter>& chapters) {
    Manga out = mangaFrom(manga);
    out.unreadCount = 0;
    out.downloadCount = static_cast<int>(chapters.size());
    for (const Chapter& chapter : chapters) {
        if (!chapter.isRead) {
            ++out.unreadCount;
        }
        out.lastReadAt = std::max(out.lastReadAt, chapter.lastReadAt);
    }
    return out;
}

int chaptersSkipped(const Chapter& from, const Chapter& to) {
    const std::int64_t gap = static_cast<std::int64_t>(to.sourceOrder) - from.sourceOrder - 1;
    return gap > 0 ? static_cast<int>(std::min<std::int64_t>(gap, 1000000)) : 0;
}

bool titleMatches(std::string_view title, std::string_view search) {
    if (search.empty()) {
        return true;
    }
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    const auto found = std::search(title.begin(), title.end(), search.begin(), search.end(),
                                   [&](char a, char b) { return lower(a) == lower(b); });
    return found != title.end();
}

}  // namespace snx::core
