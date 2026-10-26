#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "ApiTestKit.hpp"
#include "Fixtures.hpp"
#include "api/SuwayomiClient.hpp"
#include "net/FakeHttpClient.hpp"

// SuwayomiClient on a FakeHttpClient: requests as sent, recorded answers (tests/fixtures/responses/) read into models, every
// auth mode, the ui_login renewal (single flight, refusal, fallback login), errors, cancelling and delivery.
// With the fake client and no Deliver, a call completes inside the call itself, so get() never waits.
using namespace snx::api;
using snx::core::AuthMode;
using snx::core::ErrorKind;
using snx::core::ServerSettings;
using snx::net::CancelToken;
using snx::net::FakeHttpClient;
using snx::net::HttpResult;
using snx::net::Method;
using snx::net::Priority;
using snx::net::Request;

namespace {

const std::string kBase = "http://192.168.1.10:4567";
const std::string kNewToken = "<access-token>";  // what app-login.json and app-refresh-token.json hand out

ServerSettings settingsFor(AuthMode mode) {
    ServerSettings settings;
    settings.url = "192.168.1.10:4567/";  // as typed: the client normalizes it
    settings.auth = mode;
    if (mode != AuthMode::None) {
        settings.username = "snx";
        settings.password = "pw";
    }
    if (mode == AuthMode::UiLogin) {
        settings.accessToken = "old";
        settings.refreshToken = "refresh-1";
    }
    return settings;
}

HttpResult fixture(const std::string& name) {
    return FakeHttpClient::answer(200, readFixture("responses/" + name));
}

// A GraphQL answer with one error, worded the way Suwayomi does it (prefix + Java stack trace).
HttpResult gqlError(const std::string& field, const std::string& text) {
    const nlohmann::json error = {
        {"message", "Exception while fetching data (/" + field + ") : " + text + "\r\n\r\njava.lang.Exception: " + text},
        {"path", {field}}};
    return FakeHttpClient::answer(200, nlohmann::json{{"errors", {error}}}.dump());
}

std::string authOf(const Request& request) {
    return headerOf(request, "Authorization");
}

// A client on a fake HTTP client, and every settings change it reported.
struct Harness {
    FakeHttpClient http;
    std::vector<ServerSettings> saved;
    std::unique_ptr<SuwayomiClient> client;

    explicit Harness(ServerSettings settings, Deliver deliver = {}) {
        ClientOptions options;  // the full profile's page sizes: 60 manga, 100 chapters
        options.deliver = std::move(deliver);
        options.onSettingsChanged = [this](const ServerSettings& changed) { saved.push_back(changed); };
        client = std::make_unique<SuwayomiClient>(http, std::move(settings), std::move(options));
    }

    std::vector<Request> sent(const std::string& operation) const {
        std::vector<Request> found;
        for (const Request& request : http.requests()) {
            if (operationOf(request) == operation) {
                found.push_back(request);
            }
        }
        return found;
    }
    int count(const std::string& operation) const { return static_cast<int>(sent(operation).size()); }
};

// A ui_login server: kNewToken works, any other token is expired. Refreshing and logging in both work.
HttpResult uiLoginServer(const Request& request) {
    const std::string operation = operationOf(request);
    if (operation == "RefreshToken") {
        return fixture("app-refresh-token.json");
    }
    if (operation == "Login") {
        return fixture("app-login.json");
    }
    if (authOf(request) != "Bearer " + kNewToken) {
        return fixture("unauthorized.json");
    }
    if (operation == "Categories") {
        return fixture("app-categories.json");
    }
    return FakeHttpClient::answer(404);
}

}  // namespace

TEST_SUITE("api") {

TEST_CASE("client: serverInfo is a POST to {url}/api/graphql, a query net may retry, JSON in and out") {
    Harness h(settingsFor(AuthMode::None));
    h.http.setHandler([](const Request&) { return fixture("app-about-server.json"); });
    const ApiResult<snx::core::ServerInfo> info = h.client->serverInfo(CancelToken()).get();
    REQUIRE(info.ok());
    CHECK(info.value().version == "v2.4.2366");
    CHECK_FALSE(serverWarning(info.value()).has_value());

    REQUIRE(h.http.requests().size() == 1);
    const Request sent = h.http.requests()[0];
    CHECK(sent.method == Method::Post);
    CHECK(sent.url == kBase + "/api/graphql");
    CHECK(sent.idempotent);
    CHECK(headerOf(sent, "Content-Type") == "application/json");
    CHECK(headerOf(sent, "Accept") == "application/json");
    CHECK(authOf(sent).empty());  // auth none
    CHECK(operationOf(sent) == "AboutServer");
}

TEST_CASE("client: library, chapters, manga and categories read the recorded answers; pages come from the profile sizes") {
    Harness h(settingsFor(AuthMode::None));
    h.http.setHandler([](const Request& request) {
        const std::string operation = operationOf(request);
        if (operation == "Categories") return fixture("app-categories.json");
        if (operation == "LibraryByCategory") return fixture("app-library-reading.json");
        if (operation == "Chapters") return fixture("app-chapters.json");
        if (operation == "Manga") return fixture("app-manga.json");
        return FakeHttpClient::answer(404);
    });
    const CancelToken cancel;

    const auto categories = h.client->categories(cancel).get();
    REQUIRE(categories.ok());
    REQUIRE(categories.value().size() >= 3);
    CHECK(categories.value()[0].id == snx::core::kDefaultCategoryId);

    const auto library = h.client->library(1, 2, cancel).get();
    REQUIRE(library.ok());
    REQUIRE_FALSE(library.value().nodes.empty());
    CHECK(library.value().nodes[0].title == "Alpha");
    const nlohmann::json libraryVars = variablesOf(h.sent("LibraryByCategory").at(0));
    CHECK(libraryVars["first"] == 60);
    CHECK(libraryVars["offset"] == 120);  // page 2 of 60
    CHECK(libraryVars["filter"]["categoryId"]["equalTo"] == 1);

    const auto chapters = h.client->chapters(3, 1, cancel).get();
    REQUIRE(chapters.ok());
    CHECK_FALSE(chapters.value().nodes.empty());
    const nlohmann::json chapterVars = variablesOf(h.sent("Chapters").at(0));
    CHECK(chapterVars["mangaId"] == 3);
    CHECK(chapterVars["first"] == 100);
    CHECK(chapterVars["offset"] == 100);
    CHECK(chapterVars["order"] == "DESC");  // newest first

    const auto manga = h.client->manga(1, cancel).get();
    REQUIRE(manga.ok());
    CHECK(manga.value().id == 1);
    CHECK(manga.value().title == "Alpha");

    // A negative page counts as page 0.
    CHECK(h.client->library(1, -5, cancel).get().ok());
    CHECK(variablesOf(h.sent("LibraryByCategory").at(1))["offset"] == 0);
}

TEST_CASE("client: pages() and setProgress() are mutations; page format and progress are sent as asked") {
    Harness h(settingsFor(AuthMode::None));
    h.http.setHandler([](const Request& request) {
        const std::string operation = operationOf(request);
        if (operation == "FetchChapterPages") return fixture("app-fetch-chapter-pages.json");
        if (operation == "UpdateChapterProgress") return fixture("app-update-chapter-progress.json");
        return FakeHttpClient::answer(404);
    });
    const CancelToken cancel;

    const auto pages = h.client->pages(1, PageFormat::AsStored, cancel).get();
    REQUIRE(pages.ok());
    REQUIRE_FALSE(pages.value().pages.empty());
    CHECK(h.client->absoluteUrl(pages.value().pages[0]).rfind(kBase + "/api/v1/manga/", 0) == 0);
    CHECK(h.client->pages(1, PageFormat::Jpeg, cancel).get().ok());
    const std::vector<Request> pageRequests = h.sent("FetchChapterPages");
    REQUIRE(pageRequests.size() == 2);
    CHECK_FALSE(pageRequests[0].idempotent);  // a mutation is never repeated by net
    CHECK(variablesOf(pageRequests[0])["chapterId"] == 1);
    CHECK_FALSE(variablesOf(pageRequests[0]).contains("format"));
    CHECK(variablesOf(pageRequests[1])["format"] == "image/jpeg");

    const auto progress = h.client->setProgress(7, -3, true, cancel).get();
    REQUIRE(progress.ok());
    CHECK(progress.value().chapterId == 7);
    CHECK(progress.value().pageCount == 2);
    const Request update = h.sent("UpdateChapterProgress").at(0);
    CHECK_FALSE(update.idempotent);
    const nlohmann::json vars = variablesOf(update);
    CHECK(vars["id"] == 7);
    CHECK(vars["patch"]["lastPageRead"] == 0);  // below 0 is sent as 0
    CHECK(vars["patch"]["isRead"] == true);
}

TEST_CASE("client: absoluteUrl and settings use the normalized base URL, with a sub-path") {
    ServerSettings settings = settingsFor(AuthMode::None);
    settings.url = " https://NAS.local/suwayomi/ ";
    Harness h(settings);
    CHECK(h.client->settings().url == "https://nas.local/suwayomi");
    CHECK(h.client->absoluteUrl("/api/v1/manga/1/thumbnail") == "https://nas.local/suwayomi/api/v1/manga/1/thumbnail");
    h.http.setHandler([](const Request&) { return fixture("app-about-server.json"); });
    CHECK(h.client->serverInfo(CancelToken()).get().ok());
    CHECK(h.http.requests().at(0).url == "https://nas.local/suwayomi/api/graphql");
}

TEST_CASE("client: basic auth goes on every GraphQL request and every image request to the server, and nowhere else") {
    Harness h(settingsFor(AuthMode::Basic));
    h.http.setHandler([](const Request& request) {
        if (request.method == Method::Get) return FakeHttpClient::answer(200, "jpeg bytes");
        return fixture("app-categories.json");
    });
    const std::string basic = basicAuthHeader("snx", "pw");
    CHECK(h.client->categories(CancelToken()).get().ok());
    CHECK(authOf(h.http.requests().at(0)) == basic);

    // Images (the image module sends them straight through the HttpClient) get the header from the decorator.
    Request image;
    image.url = h.client->absoluteUrl("/api/v1/manga/1/thumbnail");
    CHECK(h.http.submit(image, Priority::Reader, CancelToken()).get().ok());
    CHECK(authOf(h.http.requests().at(1)) == basic);
    // A request to another host never carries the login.
    Request elsewhere;
    elsewhere.url = "http://cdn.example/icon.png";
    CHECK(h.http.submit(elsewhere, Priority::Reader, CancelToken()).get().ok());
    CHECK(authOf(h.http.requests().at(2)).empty());
}

TEST_CASE("client: basic auth refused (HTTP 401) is Auth, with no retry") {
    Harness h(settingsFor(AuthMode::Basic));
    h.http.setHandler([](const Request&) { return FakeHttpClient::answer(401, "Unauthorized"); });
    const auto result = h.client->categories(CancelToken()).get();
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().kind == ApiErrorKind::Auth);
    CHECK(result.error().detail == "HTTP 401: Unauthorized");
    CHECK(h.http.requests().size() == 1);
}

TEST_CASE("client: errors become ApiErrors") {
    Harness h(settingsFor(AuthMode::None));
    const CancelToken cancel;
    SUBCASE("GraphQL errors[] with no data: Server, with the server's words") {
        h.http.setHandler([](const Request&) { return fixture("error-manga-not-found.json"); });
        const auto result = h.client->manga(999999, cancel).get();
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().kind == ApiErrorKind::Server);
        CHECK(result.error().message.find("non null type") != std::string::npos);
    }
    SUBCASE("no connection: Offline") {
        h.http.setHandler([](const Request&) { return HttpResult::fail(ErrorKind::Network, "Connection refused"); });
        const auto result = h.client->categories(cancel).get();
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().kind == ApiErrorKind::Offline);
        CHECK(result.error().detail == "Connection refused");
    }
    SUBCASE("too slow: Timeout") {
        h.http.setHandler([](const Request&) { return HttpResult::fail(ErrorKind::Timeout, "30 s"); });
        CHECK(h.client->categories(cancel).get().error().kind == ApiErrorKind::Timeout);
    }
    SUBCASE("an answer over the limit: Parse") {
        h.http.setHandler([](const Request&) { return HttpResult::fail(ErrorKind::TooLarge, "over 8 MiB"); });
        CHECK(h.client->categories(cancel).get().error().kind == ApiErrorKind::Parse);
    }
    SUBCASE("not Suwayomi (a web page with HTTP 200): Parse") {
        h.http.setHandler([](const Request&) { return FakeHttpClient::answer(200, "<html>router login</html>"); });
        CHECK(h.client->categories(cancel).get().error().kind == ApiErrorKind::Parse);
    }
    SUBCASE("wrong sub-path (HTTP 404): Server, telling the user to check the URL") {
        h.http.setHandler([](const Request&) { return FakeHttpClient::answer(404, "Not Found"); });
        const auto result = h.client->categories(cancel).get();
        CHECK(result.error().kind == ApiErrorKind::Server);
        CHECK(result.error().message.find("URL") != std::string::npos);
    }
    SUBCASE("server crash (HTTP 500): Server") {
        h.http.setHandler([](const Request&) { return FakeHttpClient::answer(500, "boom"); });
        CHECK(h.client->categories(cancel).get().error().kind == ApiErrorKind::Server);
    }
    SUBCASE("a login error in auth none (the server wants a login): Auth, nothing renewed") {
        h.http.setHandler([](const Request&) { return fixture("unauthorized.json"); });
        CHECK(h.client->categories(cancel).get().error().kind == ApiErrorKind::Auth);
        CHECK(h.http.requests().size() == 1);
    }
}

TEST_CASE("client: login is sent without a token, keeps the tokens and reports them") {
    Harness h(settingsFor(AuthMode::UiLogin));  // still holds an old token: the server would refuse a login carrying it
    h.http.setHandler(uiLoginServer);
    const auto tokens = h.client->login("snx", "new-pw", CancelToken()).get();
    REQUIRE(tokens.ok());
    CHECK(tokens.value().accessToken == kNewToken);
    CHECK(tokens.value().refreshToken == "<refresh-token>");

    const Request sent = h.sent("Login").at(0);
    CHECK(authOf(sent).empty());
    CHECK_FALSE(sent.idempotent);
    CHECK(variablesOf(sent)["username"] == "snx");

    CHECK(h.client->settings().accessToken == kNewToken);
    CHECK(h.client->settings().password == "new-pw");
    REQUIRE(h.saved.size() == 1);
    CHECK(h.saved[0].accessToken == kNewToken);
    CHECK(h.saved[0].refreshToken == "<refresh-token>");
    CHECK(h.saved[0].password == "new-pw");

    // Later calls carry the new token.
    CHECK(h.client->categories(CancelToken()).get().ok());
    CHECK(authOf(h.sent("Categories").at(0)) == "Bearer " + kNewToken);
}

TEST_CASE("client: a wrong password is Auth, with the server's words") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler([](const Request&) { return fixture("login-wrong-password.json"); });
    const auto tokens = h.client->login("snx", "wrong", CancelToken()).get();
    REQUIRE_FALSE(tokens.ok());
    CHECK(tokens.error().kind == ApiErrorKind::Auth);
    CHECK(tokens.error().message == "Incorrect username or password.");
    CHECK(h.client->settings().accessToken == "old");  // nothing changed
    CHECK(h.saved.empty());
}

TEST_CASE("client: an expired token is refreshed once, then the call is repeated with the new token") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler(uiLoginServer);
    const auto categories = h.client->categories(CancelToken()).get();
    REQUIRE(categories.ok());
    CHECK(categories.value().size() >= 3);

    const std::vector<Request> all = h.http.requests();
    REQUIRE(all.size() == 3);
    CHECK(operationOf(all[0]) == "Categories");
    CHECK(authOf(all[0]) == "Bearer old");
    CHECK(operationOf(all[1]) == "RefreshToken");
    CHECK(authOf(all[1]).empty());
    CHECK(variablesOf(all[1])["refreshToken"] == "refresh-1");
    CHECK(operationOf(all[2]) == "Categories");
    CHECK(authOf(all[2]) == "Bearer " + kNewToken);

    CHECK(h.client->settings().accessToken == kNewToken);
    CHECK(h.client->settings().refreshToken == "refresh-1");  // refreshToken hands out only a new access token
    REQUIRE(h.saved.size() == 1);
    CHECK(h.saved[0].accessToken == kNewToken);
}

TEST_CASE("client: HTTP 401 on a GraphQL call under ui_login (a proxy in front) renews the same way") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler([](const Request& request) {
        if (operationOf(request) == "Categories" && authOf(request) == "Bearer old") {
            return FakeHttpClient::answer(401, "Unauthorized");
        }
        return uiLoginServer(request);
    });
    CHECK(h.client->categories(CancelToken()).get().ok());
    CHECK(h.count("RefreshToken") == 1);
    CHECK(h.count("Categories") == 2);
}

TEST_CASE("client: five calls rejected at the same time share one refresh") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler(uiLoginServer);
    h.http.hold(true);
    std::vector<ApiFuture<std::vector<snx::core::Category>>> calls;
    for (int i = 0; i < 5; ++i) {
        calls.push_back(h.client->categories(CancelToken()));
    }
    CHECK(h.http.waitingCount() == 5);
    // Releases the five (all refused), then the one refresh, then the five repeats.
    CHECK(h.http.releaseAll() == 11);
    for (auto& call : calls) {
        REQUIRE(call.ready());
        CHECK(call.get().ok());
    }
    CHECK(h.count("RefreshToken") == 1);
    int withNewToken = 0;
    for (const Request& request : h.sent("Categories")) {
        withNewToken += authOf(request) == "Bearer " + kNewToken ? 1 : 0;
    }
    CHECK(withNewToken == 5);
}

TEST_CASE("client: refresh refused and no saved login: Auth, and later calls don't ask the server again") {
    ServerSettings settings = settingsFor(AuthMode::UiLogin);
    settings.username.clear();
    settings.password.clear();
    Harness h(settings);
    h.http.setHandler([](const Request& request) {
        if (operationOf(request) == "RefreshToken") {
            return gqlError("refreshToken", "The Token has expired on 2026-12-25T10:00:00Z.");
        }
        return uiLoginServer(request);
    });
    const auto first = h.client->categories(CancelToken()).get();
    REQUIRE_FALSE(first.ok());
    CHECK(first.error().kind == ApiErrorKind::Auth);
    CHECK(first.error().message == "Your login has expired. Please log in again.");
    CHECK(h.count("RefreshToken") == 1);
    CHECK(h.count("Login") == 0);

    const auto second = h.client->categories(CancelToken()).get();
    CHECK(second.error().kind == ApiErrorKind::Auth);
    CHECK(h.count("RefreshToken") == 1);  // remembered as refused
    CHECK(h.saved.empty());

    // A new login clears that.
    h.http.setHandler(uiLoginServer);
    CHECK(h.client->login("snx", "pw", CancelToken()).get().ok());
    CHECK(h.client->categories(CancelToken()).get().ok());
}

TEST_CASE("client: refresh refused with a saved password: logs in again and repeats the call") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler([](const Request& request) {
        if (operationOf(request) == "RefreshToken") {
            return gqlError("refreshToken", "The Token has expired on 2026-12-25T10:00:00Z.");
        }
        return uiLoginServer(request);
    });
    CHECK(h.client->categories(CancelToken()).get().ok());
    const std::vector<Request> all = h.http.requests();
    REQUIRE(all.size() == 4);
    CHECK(operationOf(all[1]) == "RefreshToken");
    CHECK(operationOf(all[2]) == "Login");
    CHECK(authOf(all[2]).empty());
    CHECK(variablesOf(all[2])["password"] == "pw");
    CHECK(authOf(all[3]) == "Bearer " + kNewToken);
    REQUIRE(h.saved.size() == 1);
    CHECK(h.saved[0].refreshToken == "<refresh-token>");  // a login hands out both tokens
}

TEST_CASE("client: the saved password no longer works either: Auth") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler([](const Request& request) {
        const std::string operation = operationOf(request);
        if (operation == "RefreshToken") return gqlError("refreshToken", "Cannot use access token to refresh");
        if (operation == "Login") return fixture("login-wrong-password.json");
        return uiLoginServer(request);
    });
    const auto result = h.client->categories(CancelToken()).get();
    CHECK(result.error().kind == ApiErrorKind::Auth);
    CHECK(result.error().detail == "Incorrect username or password.");
}

TEST_CASE("client: renewing while the server is unreachable: Offline (not Auth), and the next call tries again") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler([](const Request& request) {
        if (operationOf(request) == "RefreshToken") return HttpResult::fail(ErrorKind::Network, "Connection reset");
        return uiLoginServer(request);
    });
    CHECK(h.client->categories(CancelToken()).get().error().kind == ApiErrorKind::Offline);
    CHECK(h.client->categories(CancelToken()).get().error().kind == ApiErrorKind::Offline);
    CHECK(h.count("RefreshToken") == 2);
    h.http.setHandler(uiLoginServer);
    CHECK(h.client->categories(CancelToken()).get().ok());
}

TEST_CASE("client: a token refused again right after renewing is final (no loop)") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler([](const Request& request) {
        if (operationOf(request) == "Categories") return fixture("unauthorized.json");
        return uiLoginServer(request);
    });
    CHECK(h.client->categories(CancelToken()).get().error().kind == ApiErrorKind::Auth);
    CHECK(h.count("RefreshToken") == 1);
    CHECK(h.count("Categories") == 2);
}

TEST_CASE("client: an image refused with 401 under ui_login renews the token for the next image") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.http.setHandler([](const Request& request) {
        if (request.method == Method::Get) {
            return authOf(request) == "Bearer " + kNewToken ? FakeHttpClient::answer(200, "jpeg bytes")
                                                            : FakeHttpClient::answer(401, "Unauthorized");
        }
        return uiLoginServer(request);
    });
    Request image;
    image.url = h.client->absoluteUrl("/api/v1/manga/1/chapter/1/page/0");
    CHECK(h.http.submit(image, Priority::Reader, CancelToken()).get().value().status == 401);
    CHECK(h.count("RefreshToken") == 1);
    CHECK(h.http.submit(image, Priority::Reader, CancelToken()).get().value().status == 200);
    std::vector<Request> images;
    for (const Request& request : h.http.requests()) {
        if (request.method == Method::Get) images.push_back(request);
    }
    REQUIRE(images.size() == 2);
    CHECK(authOf(images[0]) == "Bearer old");
    CHECK(authOf(images[1]) == "Bearer " + kNewToken);
    // A 401 from another host is none of our business.
    Request elsewhere;
    elsewhere.url = "http://cdn.example/a.png";
    h.http.submit(elsewhere, Priority::Reader, CancelToken()).get();
    CHECK(h.count("RefreshToken") == 1);
}

TEST_CASE("client: logout forgets tokens and password, without the network") {
    Harness h(settingsFor(AuthMode::UiLogin));
    h.client->logout();
    CHECK(h.http.requests().empty());
    const ServerSettings now = h.client->settings();
    CHECK(now.accessToken.empty());
    CHECK(now.refreshToken.empty());
    CHECK(now.password.empty());
    CHECK(now.username == "snx");
    REQUIRE(h.saved.size() == 1);
    CHECK(h.saved[0].accessToken.empty());

    // The next call carries no token, and without a saved login it can't renew: Auth.
    h.http.setHandler(uiLoginServer);
    CHECK(h.client->categories(CancelToken()).get().error().kind == ApiErrorKind::Auth);
    CHECK(authOf(h.sent("Categories").at(0)).empty());
    CHECK(h.count("Login") == 0);
}

TEST_CASE("client: configure switches server and login for the next call") {
    Harness h(settingsFor(AuthMode::None));
    h.http.setHandler([](const Request&) { return fixture("app-about-server.json"); });
    ServerSettings other = settingsFor(AuthMode::Basic);
    other.url = "http://10.0.0.2:4567/";
    h.client->configure(other);
    CHECK(h.client->settings().url == "http://10.0.0.2:4567");
    CHECK(h.client->serverInfo(CancelToken()).get().ok());
    CHECK(h.http.requests().at(0).url == "http://10.0.0.2:4567/api/graphql");
    CHECK(authOf(h.http.requests().at(0)) == basicAuthHeader("snx", "pw"));
    CHECK(h.saved.empty());  // configure comes from the app, so it isn't reported back
}

TEST_CASE("client: results arrive through Deliver; a call cancelled before delivery ends Cancelled") {
    std::vector<std::function<void()>> queue;  // stands in for brls::sync's main-thread queue
    Harness h(settingsFor(AuthMode::None), [&queue](std::function<void()> fn) { queue.push_back(std::move(fn)); });
    h.http.setHandler([](const Request&) { return fixture("app-categories.json"); });
    const CancelToken keep;
    const CancelToken drop;
    auto kept = h.client->categories(keep);
    auto dropped = h.client->categories(drop);
    CHECK_FALSE(kept.ready());  // answered, but not delivered yet
    CHECK_FALSE(dropped.ready());
    REQUIRE(queue.size() == 2);
    drop.cancel();  // the screen went away after the answer came in
    for (auto& fn : queue) fn();
    CHECK(kept.get().ok());
    REQUIRE_FALSE(dropped.get().ok());
    CHECK(dropped.get().error().kind == ApiErrorKind::Cancelled);
}

TEST_CASE("client: settings changes are delivered too; a call cancelled before it was sent ends Cancelled") {
    std::vector<std::function<void()>> queue;
    Harness h(settingsFor(AuthMode::UiLogin), [&queue](std::function<void()> fn) { queue.push_back(std::move(fn)); });
    h.http.setHandler(uiLoginServer);
    auto login = h.client->login("snx", "pw", CancelToken());
    CHECK(h.saved.empty());
    for (std::size_t i = 0; i < queue.size(); ++i) queue[i]();  // the settings change, then the result
    CHECK(h.saved.size() == 1);
    CHECK(login.get().ok());

    queue.clear();
    const CancelToken cancelled;
    cancelled.cancel();
    auto call = h.client->categories(cancelled);
    for (auto& fn : queue) fn();
    CHECK(call.get().error().kind == ApiErrorKind::Cancelled);
    CHECK(h.count("RefreshToken") == 0);
}

TEST_CASE("client: clientOptionsFrom takes the limits from the profile") {
    snx::core::RuntimeProfile profile;
    profile.libraryPageSize = 24;
    profile.chapterPageSize = 50;
    profile.json.maxBytes = 1234;
    const ClientOptions options = clientOptionsFrom(profile);
    CHECK(options.libraryPageSize == 24);
    CHECK(options.chapterPageSize == 50);
    CHECK(options.jsonLimits.maxBytes == 1234);
    CHECK_FALSE(options.deliver);
}

}  // TEST_SUITE("api")
