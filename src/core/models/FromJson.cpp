#include "core/models/FromJson.hpp"

#include <string>

namespace snx::core {

namespace {

template <typename T>
Result<T> notAnObject(const char* what) {
    return Result<T>::fail(ErrorKind::Parse, std::string(what) + " is not a JSON object");
}

template <typename T>
Result<T> noId(const char* what) {
    return Result<T>::fail(ErrorKind::Parse, std::string(what) + " has no valid \"id\"");
}

}  // namespace

template <>
Result<ServerInfo> fromJson<ServerInfo>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<ServerInfo>("aboutServer");
    }
    ServerInfo info;
    info.name = json::string(node, "name").value_or("");
    info.version = json::string(node, "version").value_or("");
    info.buildType = json::string(node, "buildType").value_or("");
    info.buildTime = json::int64(node, "buildTime").value_or(0);
    return info;
}

template <>
Result<Category> fromJson<Category>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<Category>("a category");
    }
    const auto id = json::int32(node, "id");
    if (!id) {
        return noId<Category>("a category");
    }
    Category category;
    category.id = *id;
    category.name = json::string(node, "name").value_or("");
    category.order = json::int32(node, "order").value_or(0);
    category.isDefault = json::boolean(node, "default").value_or(*id == kDefaultCategoryId);
    return category;
}

template <>
Result<Source> fromJson<Source>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<Source>("a source");
    }
    // LongString: text on the server, but a plain number is accepted too.
    std::string id;
    if (const auto text = json::string(node, "id")) {
        if (json::asInt64(nlohmann::json(*text))) {
            id = *text;
        }
    } else if (const auto number = json::int64(node, "id")) {
        id = std::to_string(*number);
    }
    if (id.empty()) {
        return noId<Source>("a source");
    }
    Source source;
    source.id = id;
    source.name = json::string(node, "name").value_or("");
    source.displayName = json::string(node, "displayName").value_or(source.name);
    source.lang = json::string(node, "lang").value_or("");
    source.iconUrl = json::string(node, "iconUrl").value_or("");
    return source;
}

template <>
Result<Chapter> fromJson<Chapter>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<Chapter>("a chapter");
    }
    const auto id = json::int32(node, "id");
    if (!id) {
        return noId<Chapter>("a chapter");
    }
    Chapter chapter;
    chapter.id = *id;
    chapter.mangaId = json::int32(node, "mangaId").value_or(0);
    chapter.name = json::string(node, "name").value_or("");
    chapter.chapterNumber = json::number(node, "chapterNumber").value_or(-1);
    chapter.sourceOrder = json::int32(node, "sourceOrder").value_or(0);
    chapter.uploadDate = json::int64(node, "uploadDate").value_or(0);
    chapter.scanlator = json::string(node, "scanlator").value_or("");
    chapter.isRead = json::boolean(node, "isRead").value_or(false);
    chapter.isBookmarked = json::boolean(node, "isBookmarked").value_or(false);
    chapter.isDownloaded = json::boolean(node, "isDownloaded").value_or(false);
    chapter.lastPageRead = json::int32(node, "lastPageRead").value_or(0);
    chapter.pageCount = json::int32(node, "pageCount").value_or(-1);
    return chapter;
}

template <>
Result<Manga> fromJson<Manga>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<Manga>("a manga");
    }
    const auto id = json::int32(node, "id");
    if (!id) {
        return noId<Manga>("a manga");
    }
    Manga manga;
    manga.id = *id;
    manga.title = json::string(node, "title").value_or("");
    manga.thumbnailUrl = json::string(node, "thumbnailUrl").value_or("");
    manga.author = json::string(node, "author").value_or("");
    manga.artist = json::string(node, "artist").value_or("");
    manga.description = json::string(node, "description").value_or("");
    manga.genre = json::strings(node, "genre");
    manga.status = mangaStatusFromName(json::string(node, "status").value_or(""));
    manga.inLibrary = json::boolean(node, "inLibrary").value_or(false);
    manga.unreadCount = json::int32(node, "unreadCount").value_or(0);
    manga.downloadCount = json::int32(node, "downloadCount").value_or(0);
    if (const nlohmann::json* source = json::object(node, "source")) {
        if (auto parsed = fromJson<Source>(*source)) {
            manga.source = std::move(parsed).value();
        }
    }
    if (const nlohmann::json* categories = json::object(node, "categories")) {
        if (auto list = nodeListFromJson<Category>(*categories)) {
            manga.categories = std::move(list.value().nodes);
        }
    }
    if (const nlohmann::json* chapter = json::object(node, "firstUnreadChapter")) {
        if (auto parsed = fromJson<Chapter>(*chapter)) {
            manga.firstUnreadChapter = std::move(parsed).value();
        }
    }
    if (const nlohmann::json* chapter = json::object(node, "lastReadChapter")) {
        manga.lastReadAt = json::int64(*chapter, "lastReadAt").value_or(0);
    }
    return manga;
}

template <>
Result<SyncConflict> fromJson<SyncConflict>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<SyncConflict>("syncConflict");
    }
    SyncConflict conflict;
    conflict.deviceName = json::string(node, "deviceName").value_or("");
    conflict.remotePage = json::int32(node, "remotePage").value_or(0);
    return conflict;
}

template <>
Result<PageList> fromJson<PageList>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<PageList>("fetchChapterPages");
    }
    const nlohmann::json* pages = json::array(node, "pages");
    if (!pages) {
        return Result<PageList>::fail(ErrorKind::Parse, "fetchChapterPages has no \"pages\" list");
    }
    PageList list;
    list.pages.reserve(pages->size());
    for (const auto& page : *pages) {
        list.pages.push_back(page.is_string() ? page.get_ref<const std::string&>() : std::string());
    }
    if (const nlohmann::json* chapter = json::object(node, "chapter")) {
        if (auto parsed = fromJson<Chapter>(*chapter)) {
            list.chapter = std::move(parsed).value();
        }
    }
    if (const nlohmann::json* conflict = json::object(node, "syncConflict")) {
        list.syncConflict = fromJson<SyncConflict>(*conflict).value();
    }
    return list;
}

template <>
Result<ReadProgress> fromJson<ReadProgress>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<ReadProgress>("a chapter");
    }
    const auto id = json::int32(node, "id");
    if (!id) {
        return noId<ReadProgress>("a chapter");
    }
    ReadProgress progress;
    progress.chapterId = *id;
    progress.lastPageRead = json::int32(node, "lastPageRead").value_or(0);
    progress.isRead = json::boolean(node, "isRead").value_or(false);
    progress.pageCount = json::int32(node, "pageCount").value_or(-1);
    return progress;
}

template <>
Result<LoginTokens> fromJson<LoginTokens>(const nlohmann::json& node) {
    if (!node.is_object()) {
        return notAnObject<LoginTokens>("the login answer");
    }
    LoginTokens tokens;
    tokens.accessToken = json::string(node, "accessToken").value_or("");
    tokens.refreshToken = json::string(node, "refreshToken").value_or("");
    if (tokens.accessToken.empty()) {
        return Result<LoginTokens>::fail(ErrorKind::Parse, "the login answer has no access token");
    }
    return tokens;
}

}  // namespace snx::core
