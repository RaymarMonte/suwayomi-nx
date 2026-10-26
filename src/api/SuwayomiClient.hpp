#pragma once

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
#include "core/models/Auth.hpp"
#include "core/models/Category.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "core/models/NodeList.hpp"
#include "core/models/PageList.hpp"
#include "core/models/ReadProgress.hpp"
#include "core/models/ServerInfo.hpp"
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
