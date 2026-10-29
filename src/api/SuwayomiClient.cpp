#include "api/SuwayomiClient.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <mutex>
#include <utility>

#include "api/ServerUrl.hpp"
#include "core/graphql/Gql.hpp"
#include "core/graphql/Operations.hpp"

namespace snx::api {

namespace detail {

// Everything the client and its calls under way share. `http` and `options` are set once in the constructor; the rest is
// guarded by `mutex` (calls start on any thread, answers arrive on the network thread).
struct ClientState {
    net::HttpClient* http = nullptr;
    ClientOptions options;

    using RenewDone = std::function<void(const std::optional<ApiError>&)>;

    mutable std::mutex mutex;
    core::ServerSettings server;  // url already normalized
    std::string graphqlUrl;       // {url}/api/graphql
    std::string basicHeader;      // basic_auth: "Basic ...", else empty
    // +1 whenever the login changes from outside a renewal (configure, login, logout). A renewal that started before such a
    // change doesn't store its token: it would belong to the old login.
    int epoch = 0;
    bool renewing = false;
    std::vector<RenewDone> waiters;  // calls waiting for the renewal under way
    // The last renewal was refused (Auth). Until the login changes, calls fail with this at once instead of asking the
    // server again (a page of thumbnails must not cause dozens of failed logins).
    std::optional<ApiError> renewalRefused;
};

}  // namespace detail

namespace {

using detail::ClientState;
using StatePtr = std::shared_ptr<ClientState>;
using RenewDone = ClientState::RenewDone;

// "image/jpeg" is the only conversion D6 uses.
constexpr std::string_view kJpegMime = "image/jpeg";

// --- small helpers ---

std::string base64(std::string_view data) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const unsigned n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8) |
                           static_cast<unsigned char>(data[i + 2]);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (i + 1 == data.size()) {
        const unsigned n = static_cast<unsigned char>(data[i]) << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        const unsigned n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

bool hasHeader(const net::Request& request, std::string_view name) {
    for (const auto& [key, value] : request.headers) {
        if (key.size() == name.size() &&
            std::equal(key.begin(), key.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
            return true;
        }
    }
    return false;
}

// The Bearer token a request carried, or "" if it had none.
std::string bearerTokenOf(const net::Request& request) {
    constexpr std::string_view kPrefix = "Bearer ";
    for (const auto& [key, value] : request.headers) {
        if (key == "Authorization" && value.compare(0, kPrefix.size(), kPrefix) == 0) {
            return value.substr(kPrefix.size());
        }
    }
    return {};
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

ApiError cancelledError() {
    return makeApiError(ApiErrorKind::Cancelled, "cancelled");
}

// The renewal didn't work out: the user has to log in again. detail = why.
ApiError loginExpiredError(std::string detail) {
    return ApiError{ApiErrorKind::Auth, "Your login has expired. Please log in again.", std::move(detail)};
}

// A failed login: the server's own words ("Incorrect username or password.") are the best message.
ApiError loginFailedError(const ApiError& error) {
    if (error.kind != ApiErrorKind::Server) {
        return error;
    }
    return ApiError{ApiErrorKind::Auth, error.detail.empty() ? std::string(userMessage(ApiErrorKind::Auth)) : error.detail,
                    error.detail};
}

// No answer at all (or the caller gave up): renewing again later may well work, so it isn't remembered as refused.
bool isTransient(const ApiError& error) {
    return error.kind == ApiErrorKind::Offline || error.kind == ApiErrorKind::Timeout ||
           error.kind == ApiErrorKind::Cancelled;
}

// --- delivering results ---

void runDelivered(const StatePtr& state, std::function<void()> fn) {
    if (state->options.deliver) {
        state->options.deliver(std::move(fn));
    } else {
        fn();
    }
}

// Completes a call's future through `deliver`. If the caller's token fired meanwhile, the result is Cancelled instead: a
// screen that went away never gets a late answer (the check runs where the result is delivered, e.g. on the UI thread).
template <typename T>
void deliverResult(const StatePtr& state, net::Promise<ApiResult<T>> promise, ApiResult<T> result,
                   const net::CancelToken& cancel) {
    runDelivered(state, [promise, result = std::move(result), cancel]() mutable {
        if (cancel.cancelled()) {
            promise.set(ApiResult<T>(cancelledError()));
        } else {
            promise.set(std::move(result));
        }
    });
}

void notifySettings(const StatePtr& state, core::ServerSettings settings) {
    if (!state->options.onSettingsChanged) {
        return;
    }
    runDelivered(state, [state, settings = std::move(settings)] { state->options.onSettingsChanged(settings); });
}

// --- sending GraphQL ---

template <typename T>
using GqlReader = core::Result<T> (*)(const core::GqlResponse&);

// An answer -> a model: HTTP status, then the GraphQL envelope, then the operation's reader.
template <typename T>
ApiResult<T> readAnswer(const net::HttpResult& answer, const core::JsonLimits& limits, GqlReader<T> read) {
    if (!answer.ok()) {
        return fromCoreError(answer.error());
    }
    const net::Response& response = answer.value();
    if (response.status != 200) {
        return fromHttpStatus(response.status, response.body);
    }
    core::Result<core::GqlResponse> parsed = core::parseGqlResponse(response.body, limits);
    if (!parsed.ok()) {
        return fromCoreError(parsed.error());
    }
    core::Result<T> value = read(parsed.value());
    if (!value.ok()) {
        return fromCoreError(value.error());
    }
    return std::move(value).value();
}

using AnswerFn = std::function<void(const net::HttpResult& answer, const std::string& token)>;

// POSTs one GraphQL request. withToken = false for login and refreshToken: ui_login's Bearer token is left out (the server
// refuses a login while logged in); a basic_auth header is always sent. onAnswer gets the answer and the Bearer token the
// request carried ("" if none). Runs on the network thread (or inside submit for the fake client).
void postGraphql(const StatePtr& state, const core::GqlRequest& gql, bool withToken, net::Priority priority,
                 const net::CancelToken& cancel, AnswerFn onAnswer) {
    net::Request request;
    request.method = net::Method::Post;
    request.headers = {{"Content-Type", "application/json"}, {"Accept", "application/json"}};
    request.body = gql.body();
    request.idempotent = !gql.mutation;  // only queries may be repeated after a dropped connection (D37)
    std::string token;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        request.url = state->graphqlUrl;
        switch (state->server.auth) {
            case core::AuthMode::None:
                break;
            case core::AuthMode::Basic:
                request.headers.emplace_back("Authorization", state->basicHeader);
                break;
            case core::AuthMode::UiLogin:
                if (withToken && !state->server.accessToken.empty()) {
                    token = state->server.accessToken;
                    request.headers.emplace_back("Authorization", "Bearer " + token);
                }
                break;
        }
    }
    state->http->submit(std::move(request), priority, cancel)
        .then([onAnswer = std::move(onAnswer), token](const net::HttpResult& answer) { onAnswer(answer, token); });
}

// --- renewing a ui_login token (single flight) ---

// Ends the renewal: stores the new tokens (if it worked and the login didn't change meanwhile), then lets every waiting call go
// on. error = why it failed (nullopt: it worked).
void endRenewal(const StatePtr& state, int epoch, const std::optional<core::LoginTokens>& tokens,
                std::optional<ApiError> error) {
    std::vector<RenewDone> waiters;
    std::optional<core::ServerSettings> changed;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->renewing = false;
        waiters.swap(state->waiters);
        if (state->epoch == epoch) {
            if (tokens) {
                state->server.accessToken = tokens->accessToken;
                if (!tokens->refreshToken.empty()) {
                    state->server.refreshToken = tokens->refreshToken;
                }
                state->renewalRefused.reset();
                changed = state->server;
            } else if (error && !isTransient(*error)) {
                state->renewalRefused = error;
            }
        } else if (error && !isTransient(*error)) {
            // The login changed while this renewal ran (e.g. the user logged in again): the waiting calls simply try again
            // with whatever is current now.
            error.reset();
        }
    }
    if (changed) {
        notifySettings(state, *changed);
    }
    for (RenewDone& done : waiters) {
        done(error);
    }
}

// Step 2: the refresh token didn't work (or there is none). Log in again with the saved username and password.
void renewByLogin(const StatePtr& state, int epoch, const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) {  // nothing saved, or logged out
        endRenewal(state, epoch, std::nullopt, loginExpiredError("no refresh token and no saved login"));
        return;
    }
    postGraphql(state, core::gql::login(username, password), false, net::Priority::Interactive, net::CancelToken(),
                [state, epoch](const net::HttpResult& answer, const std::string&) {
                    ApiResult<core::LoginTokens> tokens =
                        readAnswer<core::LoginTokens>(answer, state->options.jsonLimits, core::gql::readLogin);
                    if (tokens.ok()) {
                        endRenewal(state, epoch, tokens.value(), std::nullopt);
                    } else if (isTransient(tokens.error())) {
                        endRenewal(state, epoch, std::nullopt, tokens.error());
                    } else {
                        endRenewal(state, epoch, std::nullopt, loginExpiredError(tokens.error().detail));
                    }
                });
}

// Step 1: a new access token from the refresh token.
void renewByRefreshToken(const StatePtr& state, int epoch, const std::string& refreshToken, const std::string& username,
                         const std::string& password) {
    postGraphql(state, core::gql::refreshToken(refreshToken), false, net::Priority::Interactive, net::CancelToken(),
                [state, epoch, username, password](const net::HttpResult& answer, const std::string&) {
                    ApiResult<std::string> access =
                        readAnswer<std::string>(answer, state->options.jsonLimits, core::gql::readRefreshToken);
                    if (access.ok()) {
                        endRenewal(state, epoch, core::LoginTokens{access.value(), {}}, std::nullopt);
                    } else if (isTransient(access.error())) {
                        endRenewal(state, epoch, std::nullopt, access.error());
                    } else {
                        // Refused (expired after 60 days, or not valid any more).
                        renewByLogin(state, epoch, username, password);
                    }
                });
}

// ui_login: the server refused the token `rejected`. Gets a new one, then calls done (nullopt = go ahead and try again).
// Only one renewal runs at a time: calls that hit an expired token meanwhile wait for it. If the token already changed since
// that request left (another call renewed it, or the user logged in), done runs at once.
void renewLogin(const StatePtr& state, const std::string& rejected, RenewDone done) {
    std::string refreshToken;
    std::string username;
    std::string password;
    int epoch = 0;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (state->server.accessToken != rejected) {
            lock.unlock();
            done(std::nullopt);
            return;
        }
        if (state->renewalRefused) {
            const ApiError refused = *state->renewalRefused;
            lock.unlock();
            done(refused);
            return;
        }
        state->waiters.push_back(std::move(done));
        if (state->renewing) {
            return;
        }
        state->renewing = true;
        refreshToken = state->server.refreshToken;
        username = state->server.username;
        password = state->server.password;
        epoch = state->epoch;
    }
    if (refreshToken.empty()) {
        renewByLogin(state, epoch, username, password);
    } else {
        renewByRefreshToken(state, epoch, refreshToken, username, password);
    }
}

// --- one API call ---

template <typename T>
struct Call {
    StatePtr state;
    core::GqlRequest gql;
    GqlReader<T> read = nullptr;
    net::CancelToken cancel;
    net::Priority priority = net::Priority::Interactive;
    net::Promise<ApiResult<T>> promise;
    bool renewed = false;  // the token was renewed once for this call already: a second refusal is final
};

template <typename T>
void sendCall(const std::shared_ptr<Call<T>>& call);

template <typename T>
void onCallAnswer(const std::shared_ptr<Call<T>>& call, const net::HttpResult& answer, const std::string& token) {
    ApiResult<T> result = readAnswer<T>(answer, call->state->options.jsonLimits, call->read);
    if (!result.ok() && result.error().kind == ApiErrorKind::Auth && !call->renewed && !call->cancel.cancelled()) {
        bool uiLogin = false;
        {
            std::lock_guard<std::mutex> lock(call->state->mutex);
            uiLogin = call->state->server.auth == core::AuthMode::UiLogin;
        }
        if (uiLogin) {
            call->renewed = true;
            renewLogin(call->state, token, [call](const std::optional<ApiError>& failed) {
                if (failed) {
                    deliverResult<T>(call->state, call->promise, *failed, call->cancel);
                } else {
                    sendCall(call);
                }
            });
            return;
        }
    }
    deliverResult<T>(call->state, call->promise, std::move(result), call->cancel);
}

template <typename T>
void sendCall(const std::shared_ptr<Call<T>>& call) {
    postGraphql(call->state, call->gql, true, call->priority, call->cancel,
                [call](const net::HttpResult& answer, const std::string& token) { onCallAnswer(call, answer, token); });
}

template <typename T>
ApiFuture<T> startCall(const StatePtr& state, core::GqlRequest gql, GqlReader<T> read, const net::CancelToken& cancel,
                       net::Priority priority) {
    auto call = std::make_shared<Call<T>>();
    call->state = state;
    call->gql = std::move(gql);
    call->read = read;
    call->cancel = cancel;
    call->priority = priority;
    ApiFuture<T> future = call->promise.future();
    sendCall(call);
    return future;
}

// page * pageSize without overflowing (a negative page counts as 0).
int offsetOf(int page, int pageSize) {
    const long long offset = static_cast<long long>(std::max(0, page)) * std::max(0, pageSize);
    return static_cast<int>(std::min<long long>(offset, INT_MAX));
}

void applySettingsLocked(ClientState& state, core::ServerSettings server) {
    server.url = normalizeServerUrl(server.url);
    state.graphqlUrl = graphqlUrl(server.url);
    state.basicHeader = server.auth == core::AuthMode::Basic ? basicAuthHeader(server.username, server.password) : "";
    state.server = std::move(server);
    state.epoch += 1;
    state.renewalRefused.reset();
}

}  // namespace

ClientOptions clientOptionsFrom(const core::RuntimeProfile& profile) {
    ClientOptions options;
    options.jsonLimits = profile.json;
    options.libraryPageSize = profile.libraryPageSize;
    options.chapterPageSize = profile.chapterPageSize;
    return options;
}

std::string basicAuthHeader(std::string_view username, std::string_view password) {
    std::string credentials(username);
    credentials += ':';
    credentials += password;
    return "Basic " + base64(credentials);
}

std::optional<std::string> serverWarning(const core::ServerInfo& info) {
    if (core::isSupportedServer(info)) {
        return std::nullopt;
    }
    const core::ServerVersion& min = core::kMinimumServerVersion;
    const std::string needed = "v" + std::to_string(min.major) + "." + std::to_string(min.minor) + "." +
                               std::to_string(min.patch);
    const std::string what = info.version.empty() ? std::string("The server didn't say which version it runs.")
                                                  : "This server runs " + info.version + ".";
    return what + " The app needs Suwayomi-Server " + needed + " or newer, so some things may not work.";
}

SuwayomiClient::SuwayomiClient(net::HttpClient& http, core::ServerSettings server, ClientOptions options)
    : state_(std::make_shared<detail::ClientState>()) {
    state_->http = &http;
    state_->options = std::move(options);
    applySettingsLocked(*state_, std::move(server));  // nobody else can see the state yet

    const std::weak_ptr<detail::ClientState> weak = state_;
    // Every request to the server that isn't GraphQL (page images, thumbnails) gets the login's header. GraphQL requests
    // carry the one postGraphql chose, so they're left alone.
    http.setRequestDecorator([weak](net::Request& request) {
        const StatePtr state = weak.lock();
        if (!state || hasHeader(request, "Authorization")) {
            return;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!isServerUrl(state->server.url, request.url) || startsWith(request.url, state->graphqlUrl)) {
            return;
        }
        if (state->server.auth == core::AuthMode::Basic) {
            request.headers.emplace_back("Authorization", state->basicHeader);
        } else if (state->server.auth == core::AuthMode::UiLogin && !state->server.accessToken.empty()) {
            request.headers.emplace_back("Authorization", "Bearer " + state->server.accessToken);
        }
    });
    // ui_login: an image answered 401 means its token expired. Renew it in the background, so the image's next try (and
    // every later image) has a valid one. GraphQL calls renew by themselves.
    http.addResponseObserver([weak](const net::Request& sent, const net::HttpResult& result) {
        const StatePtr state = weak.lock();
        if (!state || !result.ok() || result.value().status != 401) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->server.auth != core::AuthMode::UiLogin || !isServerUrl(state->server.url, sent.url) ||
                startsWith(sent.url, state->graphqlUrl)) {
                return;
            }
        }
        renewLogin(state, bearerTokenOf(sent), [](const std::optional<ApiError>&) {});
    });
}

SuwayomiClient::~SuwayomiClient() {
    state_->http->setRequestDecorator({});
}

void SuwayomiClient::configure(core::ServerSettings server) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    applySettingsLocked(*state_, std::move(server));
}

core::ServerSettings SuwayomiClient::settings() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->server;
}

std::string SuwayomiClient::absoluteUrl(std::string_view path) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return api::absoluteUrl(state_->server.url, path);
}

ApiFuture<core::ServerInfo> SuwayomiClient::serverInfo(const net::CancelToken& cancel, net::Priority priority) {
    return startCall<core::ServerInfo>(state_, core::gql::aboutServer(), core::gql::readAboutServer, cancel, priority);
}

ApiFuture<std::vector<core::Category>> SuwayomiClient::categories(const net::CancelToken& cancel, net::Priority priority) {
    return startCall<std::vector<core::Category>>(state_, core::gql::categories(), core::gql::readCategories, cancel,
                                                  priority);
}

ApiFuture<core::NodeList<core::Manga>> SuwayomiClient::library(int categoryId, int page, const net::CancelToken& cancel,
                                                               net::Priority priority) {
    const int size = state_->options.libraryPageSize;
    return startCall<core::NodeList<core::Manga>>(state_,
                                                  core::gql::libraryByCategory(categoryId, size, offsetOf(page, size)),
                                                  core::gql::readLibraryByCategory, cancel, priority);
}

ApiFuture<core::Manga> SuwayomiClient::manga(int mangaId, const net::CancelToken& cancel, net::Priority priority) {
    return startCall<core::Manga>(state_, core::gql::manga(mangaId), core::gql::readManga, cancel, priority);
}

ApiFuture<core::NodeList<core::Chapter>> SuwayomiClient::chapters(int mangaId, int page, const net::CancelToken& cancel,
                                                                  net::Priority priority) {
    const int size = state_->options.chapterPageSize;
    return startCall<core::NodeList<core::Chapter>>(state_, core::gql::chapters(mangaId, size, offsetOf(page, size)),
                                                    core::gql::readChapters, cancel, priority);
}

ApiFuture<core::PageList> SuwayomiClient::pages(int chapterId, PageFormat format, const net::CancelToken& cancel,
                                                net::Priority priority) {
    const std::string_view mime = format == PageFormat::Jpeg ? kJpegMime : std::string_view();
    return startCall<core::PageList>(state_, core::gql::fetchChapterPages(chapterId, mime),
                                     core::gql::readFetchChapterPages, cancel, priority);
}

ApiFuture<core::ReadProgress> SuwayomiClient::setProgress(int chapterId, int page, bool isRead,
                                                          const net::CancelToken& cancel, net::Priority priority) {
    core::ReadProgress progress;
    progress.chapterId = chapterId;
    progress.lastPageRead = std::max(0, page);
    progress.isRead = isRead;
    return startCall<core::ReadProgress>(state_, core::gql::updateChapterProgress(progress),
                                         core::gql::readUpdateChapterProgress, cancel, priority);
}

ApiFuture<std::vector<core::Chapter>> SuwayomiClient::updateChapters(std::vector<int> ids, core::ChapterPatch patch,
                                                                     const net::CancelToken& cancel,
                                                                     net::Priority priority) {
    if (ids.empty()) {
        // Nothing to change: no request, but the empty answer still arrives through deliver, like every other answer.
        net::Promise<ApiResult<std::vector<core::Chapter>>> promise;
        ApiFuture<std::vector<core::Chapter>> future = promise.future();
        deliverResult<std::vector<core::Chapter>>(state_, promise, std::vector<core::Chapter>(), cancel);
        return future;
    }
    return startCall<std::vector<core::Chapter>>(state_, core::gql::updateChapters(ids, patch),
                                                 core::gql::readUpdateChapters, cancel, priority);
}

ApiFuture<core::Manga> SuwayomiClient::refreshManga(int mangaId, const net::CancelToken& cancel, net::Priority priority) {
    return startCall<core::Manga>(state_, core::gql::fetchMangaAndChapters(mangaId), core::gql::readFetchMangaAndChapters,
                                  cancel, priority);
}

ApiFuture<core::LoginTokens> SuwayomiClient::login(std::string username, std::string password,
                                                   const net::CancelToken& cancel, net::Priority priority) {
    net::Promise<ApiResult<core::LoginTokens>> promise;
    ApiFuture<core::LoginTokens> future = promise.future();
    const StatePtr state = state_;
    const core::GqlRequest request = core::gql::login(username, password);
    postGraphql(state, request, false, priority, cancel,
                [state, username = std::move(username), password = std::move(password), promise,
                 cancel](const net::HttpResult& answer, const std::string&) {
                    ApiResult<core::LoginTokens> tokens =
                        readAnswer<core::LoginTokens>(answer, state->options.jsonLimits, core::gql::readLogin);
                    if (!tokens.ok()) {
                        deliverResult<core::LoginTokens>(state, promise, loginFailedError(tokens.error()), cancel);
                        return;
                    }
                    core::ServerSettings changed;
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->server.username = username;
                        state->server.password = password;
                        state->server.accessToken = tokens.value().accessToken;
                        state->server.refreshToken = tokens.value().refreshToken;
                        state->epoch += 1;
                        state->renewalRefused.reset();
                        changed = state->server;
                    }
                    notifySettings(state, std::move(changed));
                    deliverResult(state, promise, std::move(tokens), cancel);
                });
    return future;
}

void SuwayomiClient::logout() {
    core::ServerSettings changed;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->server.accessToken.clear();
        state_->server.refreshToken.clear();
        state_->server.password.clear();
        state_->epoch += 1;
        state_->renewalRefused.reset();
        changed = state_->server;
    }
    notifySettings(state_, std::move(changed));
}

}  // namespace snx::api
