#include "core/graphql/Operations.hpp"

#include <algorithm>
#include <utility>

#include "core/graphql/Documents.hpp"
#include "core/models/FromJson.hpp"
#include "core/StripMath.hpp"

namespace snx::core::gql {

namespace {

GqlRequest request(std::string_view name) {
    return makeRequest(document(name));
}

// data.<field> as an object, or the reason it isn't there.
Result<const nlohmann::json*> dataField(const GqlResponse& response, const char* field) {
    if (const nlohmann::json* node = json::object(response.data, field)) {
        return node;
    }
    if (!response.errors.empty()) {
        return response.firstError();
    }
    return Result<const nlohmann::json*>::fail(ErrorKind::Parse, std::string("the answer has no \"") + field + "\"");
}

// fromJson<T> on data.<field>.
template <typename T>
Result<T> readObject(const GqlResponse& response, const char* field) {
    Result<const nlohmann::json*> node = dataField(response, field);
    if (!node.ok()) {
        return node.error();
    }
    return fromJson<T>(*node.value());
}

template <typename T>
Result<NodeList<T>> readList(const GqlResponse& response, const char* field) {
    Result<const nlohmann::json*> node = dataField(response, field);
    if (!node.ok()) {
        return node.error();
    }
    return nodeListFromJson<T>(*node.value());
}

}  // namespace

GqlRequest aboutServer() {
    return request("AboutServer");
}

GqlRequest categories() {
    return request("Categories");
}

GqlRequest libraryByCategory(int categoryId, int first, int offset) {
    GqlRequest r = request("LibraryByCategory");
    nlohmann::json categoryFilter = nlohmann::json::object();
    if (categoryId == kDefaultCategoryId) {
        categoryFilter["isNull"] = true;
    } else {
        categoryFilter["equalTo"] = categoryId;
    }
    r.variables["filter"] = nlohmann::json{{"categoryId", categoryFilter}};
    r.variables["first"] = std::max(first, 1);
    r.variables["offset"] = std::max(offset, 0);
    return r;
}

GqlRequest manga(int mangaId) {
    GqlRequest r = request("Manga");
    r.variables["id"] = mangaId;
    return r;
}

GqlRequest chapters(int mangaId, int first, int offset, bool newestFirst) {
    GqlRequest r = request("Chapters");
    r.variables["mangaId"] = mangaId;
    r.variables["order"] = newestFirst ? "DESC" : "ASC";
    r.variables["first"] = std::max(first, 1);
    r.variables["offset"] = std::max(offset, 0);
    return r;
}

GqlRequest fetchChapterPages(int chapterId, std::string_view format) {
    GqlRequest r = request("FetchChapterPages");
    r.variables["chapterId"] = chapterId;
    if (!format.empty()) {
        r.variables["format"] = std::string(format);
    }
    return r;
}

GqlRequest updateChapterProgress(const ReadProgress& progress) {
    GqlRequest r = request("UpdateChapterProgress");
    r.variables["id"] = progress.chapterId;
    nlohmann::json patch = nlohmann::json::object();
    patch["lastPageRead"] = std::max(progress.lastPageRead, 0);
    if (progress.isRead) {
        patch["isRead"] = true;
    }
    r.variables["patch"] = patch;
    return r;
}

GqlRequest updateStripProgress(const ReadProgress& progress) {
    GqlRequest r = request("UpdateStripProgress");
    r.variables["id"] = progress.chapterId;
    nlohmann::json patch = nlohmann::json::object();
    patch["lastPageRead"] = std::max(progress.lastPageRead, 0);
    if (progress.isRead) {
        patch["isRead"] = true;
    }
    r.variables["patch"] = patch;
    r.variables["spot"] = stripSpotValue(progress.lastPageRead, progress.spot.value_or(0.0));
    return r;
}

GqlRequest updateChapters(const std::vector<int>& ids, const ChapterPatch& patch) {
    GqlRequest r = request("UpdateChapters");
    r.variables["ids"] = ids;
    nlohmann::json fields = nlohmann::json::object();
    if (patch.isRead) {
        fields["isRead"] = *patch.isRead;
    }
    if (patch.isBookmarked) {
        fields["isBookmarked"] = *patch.isBookmarked;
    }
    if (patch.lastPageRead) {
        fields["lastPageRead"] = std::max(*patch.lastPageRead, 0);
    }
    r.variables["patch"] = fields;
    return r;
}

GqlRequest fetchMangaAndChapters(int mangaId) {
    GqlRequest r = request("FetchMangaAndChapters");
    r.variables["id"] = mangaId;
    return r;
}

GqlRequest login(std::string_view username, std::string_view password) {
    GqlRequest r = request("Login");
    r.variables["username"] = std::string(username);
    r.variables["password"] = std::string(password);
    return r;
}

GqlRequest refreshToken(std::string_view refreshToken) {
    GqlRequest r = request("RefreshToken");
    r.variables["refreshToken"] = std::string(refreshToken);
    return r;
}

GqlRequest libraryUpdates(int first, int offset) {
    GqlRequest r = request("LibraryUpdates");
    r.variables["first"] = std::max(first, 1);
    r.variables["offset"] = std::max(offset, 0);
    return r;
}

GqlRequest readingHistory(int first, int offset) {
    GqlRequest r = request("ReadingHistory");
    r.variables["first"] = std::max(first, 1);
    r.variables["offset"] = std::max(offset, 0);
    return r;
}

GqlRequest updateLibrary() {
    return request("UpdateLibrary");
}

GqlRequest stopLibraryUpdate() {
    return request("StopLibraryUpdate");
}

GqlRequest libraryUpdateStatus(bool withManga) {
    GqlRequest r = request("LibraryUpdateStatus");
    r.variables["withManga"] = withManga;
    return r;
}

GqlRequest chapterState(int chapterId) {
    GqlRequest r = request("ChapterState");
    r.variables["id"] = chapterId;
    return r;
}

Result<ServerInfo> readAboutServer(const GqlResponse& response) {
    return readObject<ServerInfo>(response, "aboutServer");
}

Result<std::vector<Category>> readCategories(const GqlResponse& response) {
    Result<NodeList<Category>> list = readList<Category>(response, "categories");
    if (!list.ok()) {
        return list.error();
    }
    return std::move(list.value().nodes);
}

Result<NodeList<Manga>> readLibraryByCategory(const GqlResponse& response) {
    return readList<Manga>(response, "mangas");
}

Result<Manga> readManga(const GqlResponse& response) {
    return readObject<Manga>(response, "manga");
}

Result<NodeList<Chapter>> readChapters(const GqlResponse& response) {
    return readList<Chapter>(response, "chapters");
}

Result<PageList> readFetchChapterPages(const GqlResponse& response) {
    return readObject<PageList>(response, "fetchChapterPages");
}

Result<ReadProgress> readUpdateChapterProgress(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "updateChapter");
    if (!node.ok()) {
        return node.error();
    }
    const nlohmann::json* chapter = json::object(*node.value(), "chapter");
    if (!chapter) {
        return Result<ReadProgress>::fail(ErrorKind::Parse, "updateChapter has no \"chapter\"");
    }
    return fromJson<ReadProgress>(*chapter);
}

Result<ReadProgress> readUpdateStripProgress(const GqlResponse& response) {
    Result<ReadProgress> progress = readUpdateChapterProgress(response);
    if (!progress.ok()) {
        return progress;
    }
    const nlohmann::json* set = json::object(response.data, "setChapterMeta");
    const nlohmann::json* meta = set ? json::object(*set, "meta") : nullptr;
    if (!meta) {
        if (!response.errors.empty()) {
            return response.firstError();
        }
        return Result<ReadProgress>::fail(ErrorKind::Parse, "setChapterMeta has no \"meta\"");
    }
    ReadProgress saved = std::move(progress).value();
    if (const auto spot = parseStripSpot(json::string(*meta, "value").value_or(""))) {
        saved.spot = spot->share;
    }
    return saved;
}

Result<std::vector<Chapter>> readUpdateChapters(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "updateChapters");
    if (!node.ok()) {
        return node.error();
    }
    const nlohmann::json* list = json::array(*node.value(), "chapters");
    if (!list) {
        return Result<std::vector<Chapter>>::fail(ErrorKind::Parse, "updateChapters has no \"chapters\"");
    }
    std::vector<Chapter> chapters;
    for (const nlohmann::json& entry : *list) {
        if (auto parsed = fromJson<Chapter>(entry)) {
            chapters.push_back(std::move(parsed.value()));
        }
    }
    return chapters;
}

Result<Manga> readFetchMangaAndChapters(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "fetchMangaAndChapters");
    if (!node.ok()) {
        return node.error();
    }
    const nlohmann::json* manga = json::object(*node.value(), "manga");
    if (!manga) {
        return Result<Manga>::fail(ErrorKind::Parse, "fetchMangaAndChapters has no \"manga\"");
    }
    return fromJson<Manga>(*manga);
}

Result<NodeList<ChapterEntry>> readLibraryUpdates(const GqlResponse& response) {
    return readList<ChapterEntry>(response, "chapters");
}

Result<NodeList<ChapterEntry>> readReadingHistory(const GqlResponse& response) {
    return readList<ChapterEntry>(response, "chapters");
}

Result<bool> readUpdateLibrary(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "updateLibrary");
    if (!node.ok()) {
        return node.error();
    }
    return true;
}

Result<bool> readStopLibraryUpdate(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "updateStop");
    if (!node.ok()) {
        return node.error();
    }
    return true;
}

Result<LibraryUpdateStatus> readLibraryUpdateStatus(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "libraryUpdateStatus");
    if (!node.ok()) {
        return node.error();
    }
    return fromJson<LibraryUpdateStatus>(response.data);
}

Result<ServerProgress> readChapterState(const GqlResponse& response) {
    Result<Chapter> chapter = readObject<Chapter>(response, "chapter");
    if (!chapter.ok()) {
        return chapter.error();
    }
    ServerProgress state;
    state.page = chapter.value().lastPageRead;
    state.read = chapter.value().isRead;
    state.lastReadAt = chapter.value().lastReadAt;
    state.spotNote = chapter.value().stripSpotNote;
    return state;
}

Result<LoginTokens> readLogin(const GqlResponse& response) {
    return readObject<LoginTokens>(response, "login");
}

Result<std::string> readRefreshToken(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "refreshToken");
    if (!node.ok()) {
        return node.error();
    }
    std::string token = json::string(*node.value(), "accessToken").value_or("");
    if (token.empty()) {
        return Result<std::string>::fail(ErrorKind::Parse, "refreshToken has no access token");
    }
    return token;
}

}  // namespace snx::core::gql
