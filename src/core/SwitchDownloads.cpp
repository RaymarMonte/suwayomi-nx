#include "core/SwitchDownloads.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>

#include "core/RuntimeProfile.hpp"

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

std::string text(const nlohmann::json& node, std::string_view key, std::size_t max = kSwitchTextMaxBytes) {
    return cut(json::string(node, key).value_or(""), max);
}

std::uint64_t unsignedOf(const nlohmann::json& node, std::string_view key) {
    return static_cast<std::uint64_t>(std::max<std::int64_t>(json::int64(node, key).value_or(0), 0));
}

double finiteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

// The chapter's own fields; with `files` also what was downloaded (formats, bytes, when, damaged pages).
nlohmann::json chapterToJson(const SwitchChapter& chapter, bool files) {
    nlohmann::json node = nlohmann::json::object();
    node["id"] = chapter.id;
    node["name"] = chapter.name;
    node["chapterNumber"] = finiteOr(chapter.chapterNumber, -1);
    node["sourceOrder"] = chapter.sourceOrder;
    node["scanlator"] = chapter.scanlator;
    node["uploadDate"] = chapter.uploadDate;
    if (files) {
        node["formats"] = chapter.formats;
        node["bytes"] = chapter.bytes;
        node["downloadedAt"] = chapter.downloadedAt;
        if (!chapter.damaged.empty()) {
            node["damaged"] = chapter.damaged;
        }
    }
    return node;
}

std::optional<SwitchChapter> chapterFromJson(const nlohmann::json& node, bool files, std::string& why) {
    if (!node.is_object()) {
        why = "not an object";
        return std::nullopt;
    }
    SwitchChapter chapter;
    chapter.id = json::int32(node, "id").value_or(0);
    if (chapter.id <= 0) {
        why = "no chapter id";
        return std::nullopt;
    }
    chapter.name = text(node, "name");
    chapter.chapterNumber = finiteOr(json::number(node, "chapterNumber").value_or(-1), -1);
    chapter.sourceOrder = json::int32(node, "sourceOrder").value_or(0);
    chapter.scanlator = text(node, "scanlator");
    chapter.uploadDate = std::max<std::int64_t>(json::int64(node, "uploadDate").value_or(0), 0);
    if (!files) {
        return chapter;
    }
    const std::string formats = json::string(node, "formats").value_or("");
    if (formats.size() > static_cast<std::size_t>(kSwitchPagesMax)) {
        why = "more than " + std::to_string(kSwitchPagesMax) + " pages";
        return std::nullopt;
    }
    if (!std::all_of(formats.begin(), formats.end(), isPageFormat)) {
        why = "a page format it doesn't know";
        return std::nullopt;
    }
    chapter.formats = formats;
    chapter.bytes = unsignedOf(node, "bytes");
    chapter.downloadedAt = std::max<std::int64_t>(json::int64(node, "downloadedAt").value_or(0), 0);
    if (const nlohmann::json* damaged = json::array(node, "damaged")) {
        std::set<int> pages;
        for (const nlohmann::json& entry : *damaged) {
            const auto page = json::asInt32(entry);
            if (page && *page >= 0 && *page < chapter.pages()) {
                pages.insert(*page);
            }
        }
        chapter.damaged.assign(pages.begin(), pages.end());
    }
    return chapter;
}

void sortChapters(std::vector<SwitchChapter>& chapters) {
    std::stable_sort(chapters.begin(), chapters.end(),
                     [](const SwitchChapter& a, const SwitchChapter& b) { return a.sourceOrder < b.sourceOrder; });
}

// One JSON value per line inside a list: the files stay small and readable in a text editor.
std::string listLines(const std::vector<nlohmann::json>& items) {
    std::string out = "[";
    bool first = true;
    for (const nlohmann::json& item : items) {
        out += first ? "\n" : ",\n";
        out += dumpJson(item);
        first = false;
    }
    out += items.empty() ? "]" : "\n]";
    return out;
}

}  // namespace

// --- Names ---

std::string downloadServerUrl(std::string_view url) {
    while (!url.empty() && url.back() == '/') {
        url.remove_suffix(1);
    }
    return std::string(url);
}

std::string switchServerKey(std::string_view url) {
    const std::string normal = downloadServerUrl(url);
    std::uint64_t hash = 14695981039346656037ULL;  // FNV-1a, 64 bit
    for (const char c : normal) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    char hex[17] = {};
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
    return hex;
}

std::string switchServerDir(const std::string& key) {
    return std::string(kSwitchDownloadsDir) + "/" + key;
}

std::string switchMangaDir(const std::string& key, int mangaId) {
    return switchServerDir(key) + "/m" + std::to_string(mangaId);
}

std::string switchChapterDir(const std::string& key, int mangaId, int chapterId) {
    return switchMangaDir(key, mangaId) + "/c" + std::to_string(chapterId);
}

std::optional<int> idOfFolder(std::string_view name, char prefix) {
    if (name.size() < 2 || name.size() > 10 || name.front() != prefix) {
        return std::nullopt;
    }
    long long id = 0;
    for (const char c : name.substr(1)) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        id = id * 10 + (c - '0');
    }
    if (id <= 0 || id > 2147483647LL) {
        return std::nullopt;
    }
    return static_cast<int>(id);
}

bool isPageFormat(char letter) {
    return letter == kPageJpeg || letter == kPagePng || letter == kPageWebp || letter == kPageGif || letter == kPageUnusable;
}

namespace {
const char* extensionOf(char format) {
    switch (format) {
        case kPageJpeg: return "jpg";
        case kPagePng: return "png";
        case kPageWebp: return "webp";
        case kPageGif: return "gif";
        default: return nullptr;
    }
}
}  // namespace

std::string switchPageName(int index, char format) {
    const char* extension = extensionOf(format);
    if (extension == nullptr || index < 0) {
        return {};
    }
    char name[32] = {};
    std::snprintf(name, sizeof(name), "%04d.%s", index, extension);
    return name;
}

std::optional<PageName> parsePageName(std::string_view name) {
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos || dot < 4 || dot > 5) {
        return std::nullopt;
    }
    int index = 0;
    for (const char c : name.substr(0, dot)) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        index = index * 10 + (c - '0');
    }
    if (index >= kSwitchPagesMax) {
        return std::nullopt;
    }
    const std::string_view extension = name.substr(dot + 1);
    for (const char format : {kPageJpeg, kPagePng, kPageWebp, kPageGif}) {
        if (extension == extensionOf(format) && switchPageName(index, format) == name) {
            return PageName{index, format};
        }
    }
    return std::nullopt;
}

// --- What is on the SD card ---

SwitchChapter switchChapterFrom(const Chapter& chapter) {
    SwitchChapter out;
    out.id = chapter.id;
    out.name = cut(chapter.name, kSwitchTextMaxBytes);
    out.chapterNumber = finiteOr(chapter.chapterNumber, -1);
    out.sourceOrder = chapter.sourceOrder;
    out.scanlator = cut(chapter.scanlator, kSwitchTextMaxBytes);
    out.uploadDate = std::max<std::int64_t>(chapter.uploadDate, 0);
    return out;
}

Chapter chapterFrom(const SwitchChapter& chapter, int mangaId) {
    Chapter out;
    out.id = chapter.id;
    out.mangaId = mangaId;
    out.name = chapter.name;
    out.chapterNumber = chapter.chapterNumber;
    out.sourceOrder = chapter.sourceOrder;
    out.scanlator = chapter.scanlator;
    out.uploadDate = chapter.uploadDate;
    out.pageCount = chapter.pages() > 0 ? chapter.pages() : -1;
    return out;
}

std::uint64_t SwitchManga::bytes() const {
    std::uint64_t total = coverBytes;
    for (const SwitchChapter& chapter : chapters) {
        total += chapter.bytes;
    }
    return total;
}

const SwitchChapter* SwitchManga::chapter(int chapterId) const {
    for (const SwitchChapter& c : chapters) {
        if (c.id == chapterId) {
            return &c;
        }
    }
    return nullptr;
}

SwitchManga switchMangaFrom(const Manga& manga) {
    SwitchManga out;
    out.id = manga.id;
    out.title = cut(manga.title, kSwitchTextMaxBytes);
    out.author = cut(manga.author, kSwitchTextMaxBytes);
    out.artist = cut(manga.artist, kSwitchTextMaxBytes);
    out.description = cut(manga.description, kSwitchDescriptionMaxBytes);
    for (const std::string& genre : manga.genre) {
        if (out.genres.size() >= kSwitchGenresMax) {
            break;
        }
        out.genres.push_back(cut(genre, kSwitchTextMaxBytes));
    }
    if (manga.status != MangaStatus::Unknown) {
        out.status = std::string(mangaStatusName(manga.status));
    }
    if (manga.source) {
        out.sourceId = cut(manga.source->id, kSwitchTextMaxBytes);
        out.sourceName = cut(manga.source->displayName.empty() ? manga.source->name : manga.source->displayName,
                             kSwitchTextMaxBytes);
    }
    out.thumbnailUrl = cut(manga.thumbnailUrl, kSwitchTextMaxBytes);
    return out;
}

void putChapter(SwitchManga& manga, SwitchChapter chapter) {
    removeChapter(manga, chapter.id);
    manga.chapters.push_back(std::move(chapter));
    sortChapters(manga.chapters);
}

bool removeChapter(SwitchManga& manga, int chapterId) {
    const auto before = manga.chapters.size();
    manga.chapters.erase(std::remove_if(manga.chapters.begin(), manga.chapters.end(),
                                        [chapterId](const SwitchChapter& c) { return c.id == chapterId; }),
                         manga.chapters.end());
    return manga.chapters.size() != before;
}

std::string writeMangaFile(const SwitchManga& manga) {
    nlohmann::json head = nlohmann::json::object();
    head["id"] = manga.id;
    head["title"] = manga.title;
    head["author"] = manga.author;
    head["artist"] = manga.artist;
    head["description"] = manga.description;
    head["genres"] = manga.genres;
    head["status"] = manga.status;
    head["sourceId"] = manga.sourceId;
    head["sourceName"] = manga.sourceName;
    head["thumbnailUrl"] = manga.thumbnailUrl;
    head["coverBytes"] = manga.coverBytes;
    std::vector<nlohmann::json> chapters;
    chapters.reserve(manga.chapters.size());
    for (const SwitchChapter& chapter : manga.chapters) {
        chapters.push_back(chapterToJson(chapter, true));
    }
    return "{\"version\": " + std::to_string(kSwitchFileVersion) + ",\n\"manga\": " + dumpJson(head) +
           ",\n\"chapters\": " + listLines(chapters) + "}\n";
}

MangaFileLoad readMangaFile(std::string_view textIn, const JsonLimits& limits) {
    MangaFileLoad load;
    Result<nlohmann::json> parsed = parseJson(textIn, limits);
    if (!parsed.ok()) {
        load.problems.push_back("manga.json: " + parsed.error().message);
        return load;
    }
    const nlohmann::json& root = parsed.value();
    const nlohmann::json* head = json::object(root, "manga");
    const int id = head ? json::int32(*head, "id").value_or(0) : 0;
    if (id <= 0) {
        load.problems.push_back("manga.json: no manga id");
        return load;
    }
    load.usable = true;
    SwitchManga& manga = load.manga;
    manga.id = id;
    manga.title = text(*head, "title");
    manga.author = text(*head, "author");
    manga.artist = text(*head, "artist");
    manga.description = text(*head, "description", kSwitchDescriptionMaxBytes);
    for (const std::string& genre : json::strings(*head, "genres")) {
        if (manga.genres.size() >= kSwitchGenresMax) {
            break;
        }
        manga.genres.push_back(cut(genre, kSwitchTextMaxBytes));
    }
    manga.status = text(*head, "status");
    manga.sourceId = text(*head, "sourceId");
    manga.sourceName = text(*head, "sourceName");
    manga.thumbnailUrl = text(*head, "thumbnailUrl");
    manga.coverBytes = unsignedOf(*head, "coverBytes");
    if (const nlohmann::json* list = json::array(root, "chapters")) {
        std::size_t index = 0;
        for (const nlohmann::json& node : *list) {
            std::string why;
            std::optional<SwitchChapter> chapter = chapterFromJson(node, true, why);
            if (!chapter) {
                load.skipped += 1;
                load.problems.push_back("manga.json of manga " + std::to_string(id) + ": chapter entry " +
                                        std::to_string(index) + " skipped (" + why + ")");
            } else {
                removeChapter(manga, chapter->id);  // the same chapter twice: the later one stays
                manga.chapters.push_back(std::move(*chapter));
            }
            ++index;
        }
    }
    sortChapters(manga.chapters);
    return load;
}

std::string writeServerFile(const std::vector<std::string>& urls) {
    nlohmann::json root = nlohmann::json::object();
    root["version"] = kSwitchFileVersion;
    root["urls"] = urls;
    return dumpJson(root) + "\n";
}

std::optional<std::vector<std::string>> readServerFile(std::string_view textIn, const JsonLimits& limits) {
    Result<nlohmann::json> parsed = parseJson(textIn, limits);
    if (!parsed.ok() || !json::array(parsed.value(), "urls")) {
        return std::nullopt;
    }
    std::vector<std::string> urls;
    for (const std::string& raw : json::strings(parsed.value(), "urls")) {
        std::string url = downloadServerUrl(raw);
        if (url.empty() || url.size() > kSwitchTextMaxBytes ||
            std::find(urls.begin(), urls.end(), url) != urls.end()) {
            continue;
        }
        urls.push_back(std::move(url));
        if (urls.size() >= kSwitchUrlsMax) {
            break;
        }
    }
    return urls;
}

const SwitchQueueEntry* findEntry(const SwitchQueue& queue, int chapterId) {
    for (const SwitchQueueEntry& entry : queue.entries) {
        if (entry.chapter.id == chapterId) {
            return &entry;
        }
    }
    return nullptr;
}

std::string writeQueueFile(const SwitchQueue& queue) {
    std::vector<nlohmann::json> entries;
    entries.reserve(queue.entries.size());
    for (const SwitchQueueEntry& entry : queue.entries) {
        nlohmann::json node = nlohmann::json::object();
        node["mangaId"] = entry.mangaId;
        node["manga"] = entry.mangaTitle;
        node["thumbnailUrl"] = entry.thumbnailUrl;
        node["chapter"] = chapterToJson(entry.chapter, false);
        node["failed"] = entry.failed;
        node["failedPage"] = entry.failedPage;
        node["pageCount"] = entry.pageCount;
        node["error"] = entry.error;
        entries.push_back(std::move(node));
    }
    return "{\"version\": " + std::to_string(kSwitchFileVersion) + ", \"paused\": " + (queue.paused ? "true" : "false") +
           ",\n\"entries\": " + listLines(entries) + "}\n";
}

QueueFileLoad readQueueFile(std::string_view textIn, const JsonLimits& limits, std::size_t maxEntries) {
    QueueFileLoad load;
    Result<nlohmann::json> parsed = parseJson(textIn, limits);
    if (!parsed.ok()) {
        load.problems.push_back("queue.json: " + parsed.error().message);
        return load;
    }
    const nlohmann::json& root = parsed.value();
    const nlohmann::json* list = json::array(root, "entries");
    if (!list) {
        load.problems.push_back("queue.json: no \"entries\" list");
        return load;
    }
    load.usable = true;
    load.queue.paused = json::boolean(root, "paused").value_or(false);
    const std::size_t keep = std::max<std::size_t>(maxEntries, 1);
    std::size_t index = 0;
    for (const nlohmann::json& node : *list) {
        std::string why;
        SwitchQueueEntry entry;
        entry.mangaId = node.is_object() ? json::int32(node, "mangaId").value_or(0) : 0;
        const nlohmann::json* chapterNode = node.is_object() ? json::object(node, "chapter") : nullptr;
        std::optional<SwitchChapter> chapter;
        if (entry.mangaId <= 0) {
            why = "no manga id";
        } else if (!chapterNode) {
            why = "no chapter";
        } else {
            chapter = chapterFromJson(*chapterNode, false, why);
        }
        if (!chapter) {
            load.skipped += 1;
            load.problems.push_back("queue.json: entry " + std::to_string(index) + " skipped (" + why + ")");
        } else if (findEntry(load.queue, chapter->id)) {
            load.problems.push_back("queue.json: entry " + std::to_string(index) + " repeats chapter " +
                                    std::to_string(chapter->id) + ", the first one is kept");
        } else if (load.queue.entries.size() >= keep) {
            load.skipped += 1;
        } else {
            entry.chapter = std::move(*chapter);
            entry.mangaTitle = text(node, "manga");
            entry.thumbnailUrl = text(node, "thumbnailUrl");
            entry.failed = json::boolean(node, "failed").value_or(false);
            entry.pageCount = std::clamp(json::int32(node, "pageCount").value_or(0), 0, kSwitchPagesMax);
            entry.failedPage = std::clamp(json::int32(node, "failedPage").value_or(-1), -1, kSwitchPagesMax - 1);
            entry.error = text(node, "error");
            load.queue.entries.push_back(std::move(entry));
        }
        ++index;
    }
    if (index > keep && load.queue.entries.size() == keep) {
        load.problems.push_back("queue.json: only the first " + std::to_string(keep) + " entries are kept");
    }
    return load;
}

// --- Space ---

SpaceCheck checkSpace(std::uint64_t used, std::uint64_t adding, std::uint64_t limit, std::optional<std::uint64_t> freeBytes,
                      std::uint64_t minFree) {
    if (!freeBytes) {
        return SpaceCheck::Unknown;  // like the disk cache: no write when the SD card can't say
    }
    const std::uint64_t after = used > UINT64_MAX - adding ? UINT64_MAX : used + adding;
    if (limit > 0 && after > limit) {
        return SpaceCheck::LimitFull;
    }
    if (*freeBytes < adding || *freeBytes - adding < minFree) {
        return SpaceCheck::SdFull;
    }
    return SpaceCheck::Fits;
}

std::uint64_t switchLimitBytes(const SwitchDownloadSettings& settings, std::uint64_t profileDefault) {
    if (!settings.limitGb ||
        std::find(kSwitchLimitChoicesGb.begin(), kSwitchLimitChoicesGb.end(), *settings.limitGb) ==
            kSwitchLimitChoicesGb.end()) {
        return profileDefault;
    }
    return static_cast<std::uint64_t>(*settings.limitGb) * kGiB;
}

// --- What the tab shows ---

const char* switchWaitName(SwitchWait wait) {
    switch (wait) {
        case SwitchWait::None: return "none";
        case SwitchWait::LimitFull: return "limit_full";
        case SwitchWait::SdFull: return "sd_full";
        case SwitchWait::SpaceUnknown: return "space_unknown";
        case SwitchWait::Server: return "server";
        case SwitchWait::Login: return "login";
        case SwitchWait::Memory: return "memory";
        case SwitchWait::Background: return "background";
    }
    return "none";
}

SwitchQueueView switchQueueView(int entries, int failed, bool paused, SwitchWait wait) {
    if (entries <= 0) {
        return SwitchQueueView::Idle;
    }
    if (entries - failed <= 0) {
        return SwitchQueueView::Stopped;
    }
    if (paused) {
        return SwitchQueueView::Paused;
    }
    return wait == SwitchWait::None ? SwitchQueueView::Running : SwitchQueueView::Waiting;
}

const char* switchQueueViewName(SwitchQueueView view) {
    switch (view) {
        case SwitchQueueView::Idle: return "idle";
        case SwitchQueueView::Running: return "running";
        case SwitchQueueView::Paused: return "paused";
        case SwitchQueueView::Waiting: return "waiting";
        case SwitchQueueView::Stopped: return "stopped";
    }
    return "idle";
}

std::string bytesText(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    static const char* const kUnits[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
        value /= 1024.0;
        ++unit;
    }
    if (value < 10.0) {
        const long tenths = std::lround(value * 10.0);
        if (tenths >= 100) {
            return "10 " + std::string(kUnits[unit]);
        }
        if (tenths % 10 == 0) {
            return std::to_string(tenths / 10) + " " + kUnits[unit];
        }
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " " + kUnits[unit];
    }
    return std::to_string(std::lround(value)) + " " + kUnits[unit];
}

}  // namespace snx::core
