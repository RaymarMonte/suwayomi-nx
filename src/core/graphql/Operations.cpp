#include "core/graphql/Operations.hpp"

#include <algorithm>
#include <utility>

#include "core/LibraryTools.hpp"
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
    r.asksSource = true;  // the server asks the source for the page list (unless downloaded)
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
    r.asksSource = true;
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

GqlRequest librarySearch(std::string_view text, int first, int offset) {
    GqlRequest r = request("LibraryByCategory");
    r.variables["filter"] = nlohmann::json{{"title", {{"includesInsensitive", likeEscape(text)}}}};
    r.variables["first"] = std::max(first, 1);
    r.variables["offset"] = std::max(offset, 0);
    return r;
}

GqlRequest manageCategories() {
    return request("ManageCategories");
}

GqlRequest createCategory(std::string_view name) {
    GqlRequest r = request("CreateCategory");
    r.variables["name"] = std::string(name);
    return r;
}

GqlRequest renameCategory(int categoryId, std::string_view name) {
    GqlRequest r = request("UpdateCategory");
    r.variables["id"] = categoryId;
    r.variables["name"] = std::string(name);
    return r;
}

GqlRequest moveCategory(int categoryId, int position) {
    GqlRequest r = request("UpdateCategoryOrder");
    r.variables["id"] = categoryId;
    r.variables["position"] = std::max(position, 1);
    return r;
}

GqlRequest deleteCategory(int categoryId) {
    GqlRequest r = request("DeleteCategory");
    r.variables["id"] = categoryId;
    return r;
}

GqlRequest updateMangaCategories(int mangaId, const std::vector<int>& add, const std::vector<int>& remove) {
    GqlRequest r = request("UpdateMangaCategories");
    r.variables["id"] = mangaId;
    nlohmann::json addList = nlohmann::json::array();
    nlohmann::json removeList = nlohmann::json::array();
    for (int id : add) {
        if (id != kDefaultCategoryId) {
            addList.push_back(id);
        }
    }
    for (int id : remove) {
        if (id != kDefaultCategoryId) {
            removeList.push_back(id);
        }
    }
    r.variables["add"] = addList;
    r.variables["remove"] = removeList;
    return r;
}

GqlRequest sources() {
    return request("Sources");
}

GqlRequest sourceFilters(std::string_view sourceId) {
    GqlRequest r = request("SourceFilters");
    r.variables["id"] = std::string(sourceId);
    return r;
}

std::string_view browseKindName(BrowseKind kind) {
    switch (kind) {
        case BrowseKind::Latest:
            return "LATEST";
        case BrowseKind::Search:
            return "SEARCH";
        case BrowseKind::Popular:
            break;
    }
    return "POPULAR";
}

GqlRequest browseSource(std::string_view sourceId, BrowseKind kind, int page, std::string_view query,
                        const nlohmann::json& filters) {
    GqlRequest r = request("BrowseSource");
    r.asksSource = true;
    r.variables["source"] = std::string(sourceId);
    r.variables["type"] = std::string(browseKindName(kind));
    r.variables["page"] = std::max(page, 1);
    if (kind == BrowseKind::Search) {
        if (!query.empty()) {
            r.variables["query"] = std::string(query);
        }
        if (filters.is_array() && !filters.empty()) {
            r.variables["filters"] = filters;
        }
    }
    return r;
}

GqlRequest libraryDuplicates(std::string_view title) {
    GqlRequest r = request("LibraryDuplicates");
    r.variables["title"] = likeEscape(title);
    return r;
}

GqlRequest addToLibrary(int mangaId, const std::vector<int>& categoryIds) {
    GqlRequest r = request("AddToLibrary");
    r.variables["id"] = mangaId;
    nlohmann::json list = nlohmann::json::array();
    for (int id : categoryIds) {
        if (id != kDefaultCategoryId) {
            list.push_back(id);
        }
    }
    r.variables["categories"] = list;
    return r;
}

GqlRequest removeFromLibrary(int mangaId) {
    GqlRequest r = request("RemoveFromLibrary");
    r.variables["id"] = mangaId;
    return r;
}

GqlRequest setCategoryDefault(int categoryId, bool on) {
    GqlRequest r = request("SetCategoryDefault");
    r.variables["id"] = categoryId;
    r.variables["on"] = on;
    return r;
}

GqlRequest downloadStatus() {
    return request("DownloadStatus");
}

GqlRequest enqueueChapterDownloads(const std::vector<int>& ids) {
    GqlRequest r = request("EnqueueChapterDownloads");
    r.variables["ids"] = ids;
    return r;
}

GqlRequest dequeueChapterDownload(int chapterId) {
    GqlRequest r = request("DequeueChapterDownload");
    r.variables["id"] = chapterId;
    return r;
}

GqlRequest reorderChapterDownload(int chapterId, int to) {
    GqlRequest r = request("ReorderChapterDownload");
    r.variables["chapterId"] = chapterId;
    r.variables["to"] = std::max(to, 0);
    return r;
}

GqlRequest startDownloader() {
    return request("StartDownloader");
}

GqlRequest stopDownloader() {
    return request("StopDownloader");
}

GqlRequest clearDownloader() {
    return request("ClearDownloader");
}

GqlRequest deleteDownloadedChapters(const std::vector<int>& ids) {
    GqlRequest r = request("DeleteDownloadedChapters");
    r.variables["ids"] = ids;
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
    state.bookmarked = chapter.value().isBookmarked;
    state.downloaded = chapter.value().isDownloaded;
    return state;
}

namespace {

// data.<field>.<inner> as an object (createCategory.category, updateMangaCategories.manga, ...).
Result<const nlohmann::json*> innerObject(const GqlResponse& response, const char* field, const char* inner) {
    Result<const nlohmann::json*> node = dataField(response, field);
    if (!node.ok()) {
        return node.error();
    }
    if (const nlohmann::json* object = json::object(*node.value(), inner)) {
        return object;
    }
    if (!response.errors.empty()) {
        return response.firstError();
    }
    return Result<const nlohmann::json*>::fail(ErrorKind::Parse,
                                               std::string(field) + " has no \"" + inner + "\"");
}

// The categories of an array (updateCategoryOrder.categories); broken entries are left out.
std::vector<Category> categoryArray(const nlohmann::json& list) {
    std::vector<Category> categories;
    for (const nlohmann::json& entry : list) {
        if (auto parsed = fromJson<Category>(entry)) {
            categories.push_back(std::move(parsed.value()));
        }
    }
    std::stable_sort(categories.begin(), categories.end(),
                     [](const Category& a, const Category& b) { return a.order < b.order; });
    return categories;
}

}  // namespace

Result<NodeList<Manga>> readLibrarySearch(const GqlResponse& response) {
    return readList<Manga>(response, "mangas");
}

Result<std::vector<Category>> readManageCategories(const GqlResponse& response) {
    return readCategories(response);
}

Result<Category> readCreateCategory(const GqlResponse& response) {
    Result<const nlohmann::json*> node = innerObject(response, "createCategory", "category");
    if (!node.ok()) {
        return node.error();
    }
    return fromJson<Category>(*node.value());
}

Result<Category> readRenameCategory(const GqlResponse& response) {
    Result<const nlohmann::json*> node = innerObject(response, "updateCategory", "category");
    if (!node.ok()) {
        return node.error();
    }
    return fromJson<Category>(*node.value());
}

Result<std::vector<Category>> readMoveCategory(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "updateCategoryOrder");
    if (!node.ok()) {
        return node.error();
    }
    const nlohmann::json* list = json::array(*node.value(), "categories");
    if (!list) {
        return Result<std::vector<Category>>::fail(ErrorKind::Parse, "updateCategoryOrder has no \"categories\"");
    }
    return categoryArray(*list);
}

Result<int> readDeleteCategory(const GqlResponse& response) {
    Result<const nlohmann::json*> node = innerObject(response, "deleteCategory", "category");
    if (!node.ok()) {
        if (node.error().kind == ErrorKind::Parse && response.errors.empty()) {
            // category: null without an error: the server deleted nothing (Default, or a category already gone).
            return Result<int>::fail(ErrorKind::Server, "the server deleted no category");
        }
        return node.error();
    }
    const nlohmann::json* mangas = json::array(*dataField(response, "deleteCategory").value(), "mangas");
    return mangas ? static_cast<int>(mangas->size()) : 0;
}

Result<std::vector<Category>> readUpdateMangaCategories(const GqlResponse& response) {
    Result<const nlohmann::json*> node = innerObject(response, "updateMangaCategories", "manga");
    if (!node.ok()) {
        return node.error();
    }
    Result<Manga> manga = fromJson<Manga>(*node.value());
    if (!manga.ok()) {
        return manga.error();
    }
    return std::move(manga.value().categories);
}

Result<std::vector<Source>> readSources(const GqlResponse& response, std::size_t maxSources) {
    Result<NodeList<Source>> list = readList<Source>(response, "sources");
    if (!list.ok()) {
        return list.error();
    }
    std::vector<Source> out = std::move(list.value().nodes);
    if (out.size() > maxSources) {
        out.resize(maxSources);
    }
    return out;
}

Result<std::vector<SourceFilter>> readSourceFilters(const GqlResponse& response, const FilterLimits& limits) {
    Result<const nlohmann::json*> node = dataField(response, "source");
    if (!node.ok()) {
        return node.error();
    }
    const nlohmann::json* list = json::array(*node.value(), "filters");
    if (!list) {
        return Result<std::vector<SourceFilter>>::fail(ErrorKind::Parse, "source has no \"filters\"");
    }
    return readFilterList(*list, limits);
}

Result<BrowsePage> readBrowseSource(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "fetchSourceManga");
    if (!node.ok()) {
        return node.error();
    }
    const nlohmann::json* list = json::array(*node.value(), "mangas");
    if (!list) {
        return Result<BrowsePage>::fail(ErrorKind::Parse, "fetchSourceManga has no \"mangas\"");
    }
    BrowsePage page;
    for (const nlohmann::json& entry : *list) {
        if (auto parsed = fromJson<Manga>(entry)) {
            page.manga.push_back(std::move(parsed.value()));
        }
    }
    page.hasNextPage = json::boolean(*node.value(), "hasNextPage").value_or(false);
    return page;
}

Result<std::vector<Manga>> readLibraryDuplicates(const GqlResponse& response) {
    Result<NodeList<Manga>> list = readList<Manga>(response, "mangas");
    if (!list.ok()) {
        return list.error();
    }
    return std::move(list.value().nodes);
}

Result<Manga> readAddToLibrary(const GqlResponse& response) {
    Result<const nlohmann::json*> added = innerObject(response, "added", "manga");
    if (!added.ok()) {
        return added.error();
    }
    Result<const nlohmann::json*> categories = innerObject(response, "categories", "manga");
    if (!categories.ok()) {
        return categories.error();
    }
    Result<Manga> manga = fromJson<Manga>(*added.value());
    if (!manga.ok()) {
        return manga.error();
    }
    Result<Manga> withCategories = fromJson<Manga>(*categories.value());
    if (withCategories.ok()) {
        manga.value().categories = std::move(withCategories.value().categories);
    }
    return manga;
}

Result<Manga> readRemoveFromLibrary(const GqlResponse& response) {
    Result<const nlohmann::json*> node = innerObject(response, "updateManga", "manga");
    if (!node.ok()) {
        return node.error();
    }
    return fromJson<Manga>(*node.value());
}

Result<Category> readSetCategoryDefault(const GqlResponse& response) {
    Result<const nlohmann::json*> node = innerObject(response, "updateCategory", "category");
    if (!node.ok()) {
        return node.error();
    }
    return fromJson<Category>(*node.value());
}

Result<DownloadQueue> readDownloadAnswer(const GqlResponse& response, std::size_t maxRows) {
    const nlohmann::json* status = json::object(response.data, "downloadStatus");  // the query
    if (!status && response.data.is_object()) {
        for (const auto& item : response.data.items()) {  // a mutation: its one field, then downloadStatus
            if (item.value().is_object()) {
                status = json::object(item.value(), "downloadStatus");
            }
            if (status) {
                break;
            }
        }
    }
    if (!status) {
        if (!response.errors.empty()) {
            return response.firstError();
        }
        return Result<DownloadQueue>::fail(ErrorKind::Parse, "the answer has no \"downloadStatus\"");
    }
    return readDownloadQueue(*status, maxRows);
}

Result<std::vector<Chapter>> readDeleteDownloadedChapters(const GqlResponse& response) {
    Result<const nlohmann::json*> node = dataField(response, "deleteDownloadedChapters");
    if (!node.ok()) {
        return node.error();
    }
    const nlohmann::json* list = json::array(*node.value(), "chapters");
    if (!list) {
        return Result<std::vector<Chapter>>::fail(ErrorKind::Parse, "deleteDownloadedChapters has no \"chapters\"");
    }
    std::vector<Chapter> chapters;
    for (const nlohmann::json& entry : *list) {
        if (auto parsed = fromJson<Chapter>(entry)) {
            chapters.push_back(std::move(parsed.value()));
        }
    }
    return chapters;
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
