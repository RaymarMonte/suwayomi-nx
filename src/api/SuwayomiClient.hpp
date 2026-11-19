#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/ApiError.hpp"
#include "core/AppConfig.hpp"
#include "core/Json.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/ServerDownloads.hpp"
#include "core/SourceFilters.hpp"
#include "core/graphql/Operations.hpp"
#include "core/models/Auth.hpp"
#include "core/models/Category.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/ChapterEntry.hpp"
#include "core/models/LibraryUpdate.hpp"
#include "core/models/Manga.hpp"
#include "core/models/NodeList.hpp"
#include "core/models/PageList.hpp"
#include "core/models/ReadProgress.hpp"
#include "core/models/ServerInfo.hpp"
#include "core/models/Source.hpp"
#include "net/CancelToken.hpp"
#include "net/Future.hpp"
#include "net/HttpClient.hpp"

// SuwayomiClient: the app's typed, asynchronous calls to Suwayomi-Server (WP-06, D38). Usage: src/api/README.md.
namespace snx::api {

// How a finished call reaches its caller. The app passes one that runs the function on the UI thread
// ([](std::function<void()> fn) { brls::sync(fn); }), so then() callbacks may touch views. Empty: the call completes right
// where its answer arrived, on the network thread (tests and tools).
using Deliver = std::function<void(std::function<void()>)>;

// Called (through Deliver) whenever the client changes the login it keeps: login(), a renewed ui_login token, logout().
// The app copies it into AppConfig::server and saves config.json.
using SettingsChanged = std::function<void(const core::ServerSettings&)>;

struct ClientOptions {
    core::JsonLimits jsonLimits;  // RuntimeProfile::json: size and depth limits for every answer
    int libraryPageSize = 60;     // RuntimeProfile::libraryPageSize: manga per library() page
    int chapterPageSize = 100;    // RuntimeProfile::chapterPageSize: chapters per chapters() page
    int updatesPageSize = 50;     // RuntimeProfile::updatesPageSize: chapters per updates() page (WP-15)
    int historyPageSize = 50;     // RuntimeProfile::historyPageSize: chapters per history() page
    std::size_t sourcesMax = 500; // RuntimeProfile::sourcesMax: sources() keeps at most this many (WP-14)
    std::size_t serverQueueMax = 1000;  // RuntimeProfile::serverQueueMax: queue rows the download calls keep (WP-17)
    core::FilterLimits filterLimits;  // RuntimeProfile::sourceFiltersMax, filterValuesMax, filterNameMaxBytes
    // The time limit of a request that asks the source (GqlRequest::asksSource, D60): RuntimeProfile::httpSourceTimeoutMs,
    // or Settings' choice (core::sourceWaitMs). 0 = the HTTP client's usual limit (httpTimeoutMs). setSourceTimeoutMs changes it.
    int sourceTimeoutMs = 0;
    Deliver deliver;
    SettingsChanged onSettingsChanged;
};

// The limits above, taken from the profile. deliver and onSettingsChanged stay empty.
ClientOptions clientOptionsFrom(const core::RuntimeProfile& profile);

// pages(): the page images as the server stores them, or converted to JPEG by the server (D6, ReaderSettings).
enum class PageFormat { AsStored, Jpeg };

template <typename T>
using ApiFuture = net::Future<ApiResult<T>>;

namespace detail {
struct ClientState;  // SuwayomiClient.cpp
}

// Every call:
// - takes a CancelToken. Once it fires, the call ends Cancelled, even if the answer was already there but not delivered yet.
// - completes exactly once, with a value or an ApiError (never throws), through ClientOptions::deliver.
// - goes out with the configured login: none, Basic (every request), or ui_login (Bearer token). When a ui_login token is
//   rejected, the client renews it once (refreshToken, or login with the saved password when the refresh token is rejected)
//   and repeats the call. Calls that hit an expired token at the same time share one renewal. If renewing fails: Auth.
// Queries may be retried by net after a dropped connection; mutations (setProgress, login) never are.
class SuwayomiClient {
  public:
    // Installs itself as http's request decorator (every request to the server outside /api/graphql, page images and
    // thumbnails included, gets the login's Authorization header) and adds a response observer (an image answered 401 under
    // ui_login starts a token renewal). So: one SuwayomiClient per HttpClient, kept for the HttpClient's whole life; change
    // the server or login with configure(). http must outlive this client.
    SuwayomiClient(net::HttpClient& http, core::ServerSettings server, ClientOptions options);
    ~SuwayomiClient();

    SuwayomiClient(const SuwayomiClient&) = delete;
    SuwayomiClient& operator=(const SuwayomiClient&) = delete;

    // A new server URL, auth mode, credentials or tokens (the URL is normalized with normalizeServerUrl). Calls already
    // under way finish with the old settings.
    void configure(core::ServerSettings server);
    // The settings in use now, with the latest tokens.
    core::ServerSettings settings() const;
    // D60: the time limit of calls that ask the source (GqlRequest::asksSource), from Settings -> Wait for sources. Calls
    // that start afterwards use it; 0 = the HTTP client's usual limit.
    void setSourceTimeoutMs(int ms);
    // A page or thumbnail path from the server -> its full URL (absoluteUrl with the configured base URL).
    std::string absoluteUrl(std::string_view path) const;

    ApiFuture<core::ServerInfo> serverInfo(const net::CancelToken& cancel,
                                           net::Priority priority = net::Priority::Interactive);
    ApiFuture<std::vector<core::Category>> categories(const net::CancelToken& cancel,
                                                      net::Priority priority = net::Priority::Interactive);
    // One page of a library tab, sorted by title. page 0 = the first libraryPageSize manga. categoryId 0 = Default.
    ApiFuture<core::NodeList<core::Manga>> library(int categoryId, int page, const net::CancelToken& cancel,
                                                   net::Priority priority = net::Priority::Interactive);
    ApiFuture<core::Manga> manga(int mangaId, const net::CancelToken& cancel,
                                 net::Priority priority = net::Priority::Interactive);
    // One page of a manga's chapters, newest first. page 0 = the first chapterPageSize chapters.
    ApiFuture<core::NodeList<core::Chapter>> chapters(int mangaId, int page, const net::CancelToken& cancel,
                                                      net::Priority priority = net::Priority::Interactive);
    // Opens a chapter: its page URLs (relative; use absoluteUrl). The server learns the page count here, so call it before
    // setProgress for a chapter (until then the server saves progress as 0).
    ApiFuture<core::PageList> pages(int chapterId, PageFormat format, const net::CancelToken& cancel,
                                    net::Priority priority = net::Priority::Interactive);
    // Saves reading progress: page = index of the page on screen (below 0 is sent as 0; the server caps it at the chapter's
    // page count). isRead = true marks the chapter read; false leaves the read flag alone. Answers what the server saved.
    ApiFuture<core::ReadProgress> setProgress(int chapterId, int page, bool isRead, const net::CancelToken& cancel,
                                              net::Priority priority = net::Priority::Interactive);
    // The same from a whole ReadProgress (WP-13b): with a spot (the webtoon strip) the page and the chapter meta note
    // "snx_strip_spot" go in one request (UpdateStripProgress), and the save counts only when both answered; the answer
    // carries the spot read back from the note. Without a spot it is the call above.
    ApiFuture<core::ReadProgress> setProgress(const core::ReadProgress& progress, const net::CancelToken& cancel,
                                              net::Priority priority = net::Priority::Interactive);
    // The chapter list's actions (WP-10): applies `patch` to every chapter in `ids` (mark read / unread, bookmark) in one
    // request. Answers the changed chapters as the server saved them. No ids: answers an empty list without asking the server.
    ApiFuture<std::vector<core::Chapter>> updateChapters(std::vector<int> ids, core::ChapterPatch patch,
                                                         const net::CancelToken& cancel,
                                                         net::Priority priority = net::Priority::Interactive);
    // "Refresh from source" (WP-10): the server asks the manga's source for its details and chapter list again (slow for an
    // online source; a local one only reads its folder). Answers the refreshed manga; load chapters() again after it.
    ApiFuture<core::Manga> refreshManga(int mangaId, const net::CancelToken& cancel,
                                        net::Priority priority = net::Priority::Interactive);

    // WP-15, the Updates tab: one page of the library's chapters, the most recently found first, each with its manga (id,
    // title, cover, inLibraryAt). page 0 = the first updatesPageSize. The tab keeps the new ones (core::filterUpdates).
    ApiFuture<core::NodeList<core::ChapterEntry>> updates(int page, const net::CancelToken& cancel,
                                                          net::Priority priority = net::Priority::Interactive);
    // The History tab: one page of the chapters read or with progress, the most recently read first, each with its manga
    // (id, title, cover, inLibrary). page 0 = the first historyPageSize. The tab keeps one per manga (core::historyRows).
    ApiFuture<core::NodeList<core::ChapterEntry>> history(int page, const net::CancelToken& cancel,
                                                          net::Priority priority = net::Priority::Interactive);
    // Asks the server to update the whole library (a mutation: never repeated). Answers true once the server took it;
    // follow it with libraryUpdateStatus() (its own answer is the previous job's status, so it isn't handed out).
    ApiFuture<bool> startLibraryUpdate(const net::CancelToken& cancel,
                                       net::Priority priority = net::Priority::Interactive);
    // Stops the library update that runs (a mutation).
    ApiFuture<bool> stopLibraryUpdate(const net::CancelToken& cancel,
                                      net::Priority priority = net::Priority::Interactive);
    // How far the library update is, and when the last one started. withManga: also the titles of the manga that failed.
    ApiFuture<core::LibraryUpdateStatus> libraryUpdateStatus(bool withManga, const net::CancelToken& cancel,
                                                             net::Priority priority = net::Priority::Interactive);
    // WP-15b: one chapter's reading progress on the server now (page, read flag, stamp, the strip's note): the waiting-save
    // queue's check before it sends a save that failed earlier. A chapter that is gone: Server.
    ApiFuture<core::ServerProgress> chapterState(int chapterId, const net::CancelToken& cancel,
                                                 net::Priority priority = net::Priority::Background);
    // WP-24 (D54), the library tools. One page of the library manga whose title contains `text` (any case; `%` and `_`
    // match themselves), by title, across every category. page 0 = the first libraryPageSize.
    ApiFuture<core::NodeList<core::Manga>> searchLibrary(const std::string& text, int page, const net::CancelToken& cancel,
                                                         net::Priority priority = net::Priority::Interactive);
    // Every category in the user's order (Default first) with its manga count (Category::mangaCount).
    ApiFuture<std::vector<core::Category>> manageCategories(const net::CancelToken& cancel,
                                                            net::Priority priority = net::Priority::Interactive);
    // The mutations below are never repeated. The server checks almost nothing: check names with core::checkCategoryName.
    // A new category, at the end. Answers it as the server saved it.
    ApiFuture<core::Category> createCategory(const std::string& name, const net::CancelToken& cancel,
                                             net::Priority priority = net::Priority::Interactive);
    ApiFuture<core::Category> renameCategory(int categoryId, const std::string& name, const net::CancelToken& cancel,
                                             net::Priority priority = net::Priority::Interactive);
    // Moves a category to `position` (core::categoryPosition). Answers every category in the new order.
    ApiFuture<std::vector<core::Category>> moveCategory(int categoryId, int position, const net::CancelToken& cancel,
                                                        net::Priority priority = net::Priority::Interactive);
    // Deletes a category; its manga stay in the library. Answers how many manga were in it.
    ApiFuture<int> deleteCategory(int categoryId, const net::CancelToken& cancel,
                                  net::Priority priority = net::Priority::Interactive);
    // Adds and removes one manga's categories in one request (core::categoryChange). Answers its categories now.
    ApiFuture<std::vector<core::Category>> setMangaCategories(int mangaId, std::vector<int> add, std::vector<int> remove,
                                                              const net::CancelToken& cancel,
                                                              net::Priority priority = net::Priority::Interactive);
    // WP-14 (D56), browse. Every source the server has (Local and the extensions'), at most sourcesMax.
    ApiFuture<std::vector<core::Source>> sources(const net::CancelToken& cancel,
                                                 net::Priority priority = net::Priority::Interactive);
    // A source's filters as a tree (core/SourceFilters.hpp), with the filterLimits.
    ApiFuture<std::vector<core::SourceFilter>> sourceFilters(const std::string& sourceId, const net::CancelToken& cancel,
                                                             net::Priority priority = net::Priority::Interactive);
    // One page of a source's list. page: 1-based. Search: the text and / or the filters' changes (core::filterChanges);
    // Popular and Latest ignore both. A mutation on the server (it makes manga rows): never repeated. A source failure is
    // a Server error with the source's message.
    ApiFuture<core::BrowsePage> browseSource(const std::string& sourceId, core::gql::BrowseKind kind, int page,
                                             const std::string& query, const nlohmann::json& changes,
                                             const net::CancelToken& cancel,
                                             net::Priority priority = net::Priority::Interactive);
    // Library manga titled `title` (any case; % and _ match themselves), without manga `exceptId`: the same-title warning.
    ApiFuture<std::vector<core::Manga>> libraryDuplicates(const std::string& title, int exceptId,
                                                          const net::CancelToken& cancel,
                                                          net::Priority priority = net::Priority::Interactive);
    // The mutations below are never repeated. Adds a manga to the library in exactly these categories (none = Default),
    // one request. Answers the manga with inLibrary and its categories now.
    ApiFuture<core::Manga> addToLibrary(int mangaId, std::vector<int> categoryIds, const net::CancelToken& cancel,
                                        net::Priority priority = net::Priority::Interactive);
    // Takes a manga out of the library (its categories and progress stay on the server). Answers id + inLibrary.
    ApiFuture<core::Manga> removeFromLibrary(int mangaId, const net::CancelToken& cancel,
                                             net::Priority priority = net::Priority::Interactive);
    // A category's "default" flag ("tick it for new manga"). Answers the category.
    ApiFuture<core::Category> setCategoryDefault(int categoryId, bool on, const net::CancelToken& cancel,
                                                 net::Priority priority = net::Priority::Interactive);
    // WP-17 (D58), downloads onto the server's own disk. Each answers the server's queue as it is after the call (at most
    // serverQueueMax rows kept, core::DownloadQueue). The mutations are never repeated; SUWAYOMI-API.md "Server downloads"
    // says which of them make the server wait 30 s (dequeue a chapter that isn't queued, start with nothing queued).
    ApiFuture<core::DownloadQueue> downloadStatus(const net::CancelToken& cancel,
                                                  net::Priority priority = net::Priority::Interactive);
    // Queues chapters (at most serverDownloadBatchMax ids per call); the server starts by itself about a second later.
    ApiFuture<core::DownloadQueue> enqueueDownloads(std::vector<int> ids, const net::CancelToken& cancel,
                                                    net::Priority priority = net::Priority::Interactive);
    ApiFuture<core::DownloadQueue> dequeueDownload(int chapterId, const net::CancelToken& cancel,
                                                   net::Priority priority = net::Priority::Interactive);
    // to: the 0-based place in the whole queue (0 = top, rows - 1 = bottom).
    ApiFuture<core::DownloadQueue> reorderDownload(int chapterId, int to, const net::CancelToken& cancel,
                                                   net::Priority priority = net::Priority::Interactive);
    ApiFuture<core::DownloadQueue> startDownloads(const net::CancelToken& cancel,
                                                  net::Priority priority = net::Priority::Interactive);
    ApiFuture<core::DownloadQueue> stopDownloads(const net::CancelToken& cancel,
                                                 net::Priority priority = net::Priority::Interactive);
    ApiFuture<core::DownloadQueue> clearDownloads(const net::CancelToken& cancel,
                                                  net::Priority priority = net::Priority::Interactive);
    // Deletes the chapters' downloads from the server's disk. Answers each chapter's id + isDownloaded.
    ApiFuture<std::vector<core::Chapter>> deleteServerDownloads(std::vector<int> ids, const net::CancelToken& cancel,
                                                                net::Priority priority = net::Priority::Interactive);
    // ui_login: logs in (sent without a token: the server refuses a login while logged in), keeps the tokens, username and
    // password for renewing later, and reports them through onSettingsChanged. Wrong password: Auth with the server's words.
    ApiFuture<core::LoginTokens> login(std::string username, std::string password, const net::CancelToken& cancel,
                                       net::Priority priority = net::Priority::Interactive);
    // Forgets the tokens and the password (reported through onSettingsChanged). Suwayomi has no server-side logout: its
    // tokens simply expire. No network.
    void logout();

  private:
    std::shared_ptr<detail::ClientState> state_;  // shared with the calls under way, so they finish safely after the client is gone
};

// "Basic " + base64(username ":" password), the Authorization header value for basic_auth (RFC 7617).
std::string basicAuthHeader(std::string_view username, std::string_view password);

// D9: a warning to show when the server is older than the oldest supported version (or doesn't say which it is).
// nullopt when it is fine.
std::optional<std::string> serverWarning(const core::ServerInfo& info);

}  // namespace snx::api
