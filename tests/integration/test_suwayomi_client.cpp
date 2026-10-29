#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ApiTestKit.hpp"
#include "api/SuwayomiClient.hpp"
#include "core/RuntimeProfile.hpp"
#include "image/ImageLoader.hpp"
#include "net/CurlHttpClient.hpp"

// SuwayomiClient against the real Suwayomi test server (tools/test-server, fixture library in docs/TESTING.md). The server's
// address and login come from the environment (tools/test-server/test-server.sh env): SNX_SERVER_URL, SNX_AUTH
// (none|basic|ui_login), SNX_USER, SNX_PASS. Suites: "all" runs in every auth mode, "basic" and "ui_login" only in theirs.
// tools/build.sh integration runs everything.
using namespace snx::api;
using namespace std::chrono_literals;
using snx::core::AuthMode;
using snx::core::ServerSettings;
using snx::net::CancelToken;

namespace {

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

// The test server's settings, as the setup screen would store them (ui_login: no tokens yet, the client logs in by itself).
ServerSettings serverSettings() {
    ServerSettings settings;
    settings.url = env("SNX_SERVER_URL");
    REQUIRE_MESSAGE(!settings.url.empty(), "SNX_SERVER_URL is not set: run tools/build.sh integration");
    const auto mode = snx::core::authModeFromName(env("SNX_AUTH").empty() ? "none" : env("SNX_AUTH"));
    REQUIRE_MESSAGE(mode.has_value(), "SNX_AUTH must be none, basic or ui_login");
    settings.auth = *mode;
    if (settings.auth != AuthMode::None) {
        settings.username = env("SNX_USER");
        settings.password = env("SNX_PASS");
    }
    return settings;
}

// One client with its own curl client (one SuwayomiClient per HttpClient), counting the refreshes and logins it sends.
struct Live {
    snx::core::RuntimeProfile profile;
    snx::net::CurlHttpClient http{snx::net::curlOptionsFrom(profile)};
    std::atomic<int> refreshes{0};
    std::atomic<int> logins{0};
    std::unique_ptr<SuwayomiClient> client;

    explicit Live(ServerSettings settings = serverSettings()) {
        http.addResponseObserver([this](const snx::net::Request& sent, const snx::net::HttpResult&) {
            const std::string operation = operationOf(sent.body);
            if (operation == "RefreshToken") refreshes++;
            if (operation == "Login") logins++;
        });
        client = std::make_unique<SuwayomiClient>(http, std::move(settings), clientOptionsFrom(profile));
    }
    ~Live() { client.reset(); }  // before http

    SuwayomiClient* operator->() { return client.get(); }

    // GET a server path through the same HttpClient, as the image module will (the client's decorator adds the login).
    snx::net::HttpResult get(const std::string& path) {
        snx::net::Request request;
        request.url = client->absoluteUrl(path);
        auto future = http.submit(request, snx::net::Priority::Reader, CancelToken());
        REQUIRE(future.waitFor(60s));
        return future.get();
    }
};

// Waits for a call and returns its value; fails the test with the error's detail if there is none.
template <typename T>
T must(ApiFuture<T> future) {
    REQUIRE(future.waitFor(60s));
    const ApiResult<T>& result = future.get();
    REQUIRE_MESSAGE(result.ok(), apiErrorKindName(result.error().kind) << ": " << result.error().detail);
    return result.value();
}

template <typename T>
ApiError mustFail(ApiFuture<T> future) {
    REQUIRE(future.waitFor(60s));
    const ApiResult<T>& result = future.get();
    REQUIRE_FALSE(result.ok());
    return result.error();
}

// The test server's categories (tools/test-server/seed.ts): Default, Reading, Long series, Big shelf, Empty.
constexpr std::size_t kCategories = 5;

int libraryId(Live& live, const std::string& title) {
    for (const int category : {0, 1, 2}) {
        for (const snx::core::Manga& manga : must(live->library(category, 0, CancelToken())).nodes) {
            if (manga.title == title) {
                return manga.id;
            }
        }
    }
    FAIL("no manga called " << title);
    return 0;
}

std::vector<std::string> titles(const snx::core::NodeList<snx::core::Manga>& list) {
    std::vector<std::string> out;
    for (const auto& manga : list.nodes) out.push_back(manga.title);
    return out;
}

bool isJpeg(const std::string& bytes) {
    return bytes.size() > 3 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xD8;
}

}  // namespace

TEST_SUITE("all") {

TEST_CASE("serverInfo: the pinned test server, no version warning") {
    Live live;
    const snx::core::ServerInfo info = must(live->serverInfo(CancelToken()));
    CHECK(info.name == "Suwayomi-Server");
    CHECK(info.version == "v2.4.2366");
    CHECK_FALSE(serverWarning(info).has_value());
}

TEST_CASE("categories and the three library tabs") {
    Live live;
    const std::vector<snx::core::Category> categories = must(live->categories(CancelToken()));
    REQUIRE(categories.size() == kCategories);
    CHECK(categories[0].id == 0);
    CHECK(categories[0].isDefault);
    CHECK(categories[1].name == "Reading");
    CHECK(categories[2].name == "Long series");
    CHECK(categories[3].name == "Big shelf");
    CHECK(categories[4].name == "Empty");

    // WP-09: the Big shelf in pages of 60 (60, 60, 30), never overlapping; Empty has none.
    std::vector<std::string> shelf;
    for (int page = 0; page < 3; ++page) {
        const auto part = must(live->library(categories[3].id, page, CancelToken()));
        CHECK(part.totalCount == 150);
        CHECK(part.hasNextPage == (page < 2));
        for (const std::string& title : titles(part)) {
            shelf.push_back(title);
        }
    }
    REQUIRE(shelf.size() == 150);
    CHECK(shelf.front() == "Shelf 001");
    CHECK(shelf.back() == "Shelf 150");
    CHECK(std::is_sorted(shelf.begin(), shelf.end()));
    const auto empty = must(live->library(categories[4].id, 0, CancelToken()));
    CHECK(empty.nodes.empty());
    CHECK(empty.totalCount == 0);

    const auto reading = must(live->library(categories[1].id, 0, CancelToken()));
    CHECK(titles(reading) == std::vector<std::string>{"Alpha", "Beta RTL"});
    CHECK(reading.totalCount == 2);
    CHECK_FALSE(reading.hasNextPage);
    const auto longSeries = must(live->library(categories[2].id, 0, CancelToken()));
    CHECK(titles(longSeries) == std::vector<std::string>{"Delta Big", "Gamma Webtoon"});
    const auto defaults = must(live->library(snx::core::kDefaultCategoryId, 0, CancelToken()));
    CHECK(titles(defaults) == std::vector<std::string>{"Broken", "日本語タイトル テスト"});
    // Page 1 of a short tab is empty.
    CHECK(must(live->library(categories[1].id, 1, CancelToken())).nodes.empty());
}

TEST_CASE("manga details and chapter pages of 100, newest first") {
    Live live;
    const snx::core::Manga alpha = must(live->manga(libraryId(live, "Alpha"), CancelToken()));
    CHECK(alpha.title == "Alpha");
    CHECK_FALSE(alpha.author.empty());
    CHECK_FALSE(alpha.genre.empty());
    // Not alpha.status: the server reads Alpha's "ongoing" from details.json only on the first seeding; after a restart
    // with another auth mode it says UNKNOWN (a Local Source quirk, seen 2026-10-26).

    const int delta = libraryId(live, "Delta Big");
    const auto first = must(live->chapters(delta, 0, CancelToken()));
    CHECK(first.totalCount == 300);
    REQUIRE(first.nodes.size() == 100);
    CHECK(first.hasNextPage);
    CHECK(first.nodes.front().sourceOrder == 300);
    const auto last = must(live->chapters(delta, 2, CancelToken()));
    REQUIRE(last.nodes.size() == 100);
    CHECK_FALSE(last.hasNextPage);
    CHECK(last.nodes.back().sourceOrder == 1);
    CHECK(must(live->chapters(delta, 3, CancelToken())).nodes.empty());
}

TEST_CASE("pages and thumbnails download with the login, as stored and converted to JPEG") {
    Live live;
    const int alpha = libraryId(live, "Alpha");
    const auto chapters = must(live->chapters(alpha, 0, CancelToken()));
    REQUIRE(chapters.nodes.size() == 3);
    const snx::core::PageList pages = must(live->pages(chapters.nodes.back().id, PageFormat::AsStored, CancelToken()));
    REQUIRE(pages.pages.size() == 10);
    const snx::net::HttpResult page = live.get(pages.pages[0]);
    REQUIRE(page.ok());
    CHECK(page.value().status == 200);
    CHECK(isJpeg(page.value().body));

    const snx::core::Manga manga = must(live->manga(alpha, CancelToken()));
    const snx::net::HttpResult thumbnail = live.get(manga.thumbnailUrl);
    REQUIRE(thumbnail.ok());
    CHECK(thumbnail.value().status == 200);

    // Beta RTL is PNG on disk; the server converts it when asked (D6). The URL then carries ?format=.
    const int beta = libraryId(live, "Beta RTL");
    const auto betaChapters = must(live->chapters(beta, 0, CancelToken()));
    REQUIRE_FALSE(betaChapters.nodes.empty());
    const snx::core::PageList jpeg = must(live->pages(betaChapters.nodes.back().id, PageFormat::Jpeg, CancelToken()));
    REQUIRE_FALSE(jpeg.pages.empty());
    CHECK(jpeg.pages[0].find("?format=image%2Fjpeg") != std::string::npos);
    const snx::net::HttpResult converted = live.get(jpeg.pages[0]);
    REQUIRE(converted.ok());
    CHECK(converted.value().status == 200);
    CHECK(isJpeg(converted.value().body));
}

TEST_CASE("image loader (WP-07): real pages through the login, decoded and tiled; the Broken manga fails cleanly") {
    Live live;
    // The same HttpClient the SuwayomiClient decorates, so every page request carries the login (D38).
    snx::image::LoaderOptions options = snx::image::loaderOptionsFrom(
        snx::core::makeRuntimeProfile(snx::core::LaunchMode::Application, 4 * snx::core::kGiB, 100 * snx::core::kMiB));
    snx::image::ImageLoader loader(live.http, options);  // no deliver: answers come on a loader thread
    const auto firstPage = [&](const std::string& title, int index) {
        const auto chapters = must(live->chapters(libraryId(live, title), 0, CancelToken()));
        REQUIRE_FALSE(chapters.nodes.empty());
        const auto pages = must(live->pages(chapters.nodes.back().id, PageFormat::AsStored, CancelToken()));
        REQUIRE(static_cast<int>(pages.pages.size()) > index);
        return live->absoluteUrl(pages.pages[static_cast<std::size_t>(index)]);
    };
    const auto load = [&](const std::string& url, snx::core::ImageSize box) {
        auto done = std::make_shared<std::promise<snx::image::ImageResult>>();
        auto answer = done->get_future();
        loader.load(snx::image::LoadRequest{url, box, snx::net::Priority::Interactive, snx::image::ImageKind::Page},
                    CancelToken(), [done](const snx::image::ImageResult& result) { done->set_value(result); });
        REQUIRE(answer.wait_for(60s) == std::future_status::ready);
        return answer.get();
    };

    const auto alpha = load(firstPage("Alpha", 0), {1280, 720});
    REQUIRE_MESSAGE(alpha.ok(), (alpha.ok() ? "" : alpha.error().message));
    CHECK(alpha.value()->size() == snx::core::ImageSize{500, 750});  // JPEG at 1/2, within the 15 % slack
    CHECK(alpha.value()->sourceSize() == snx::core::ImageSize{1000, 1500});
    const auto beta = load(firstPage("Beta RTL", 0), {1280, 720});
    REQUIRE(beta.ok());
    CHECK(beta.value()->format() == snx::image::ImageFormat::Png);
    const auto strip = load(firstPage("Gamma Webtoon", 0), {1280, 0});
    REQUIRE(strip.ok());
    CHECK(strip.value()->size() == snx::core::ImageSize{800, 12000});
    CHECK(strip.value()->tiles().size() == 6);

    // Broken: page 1 corrupt, page 2 empty, page 3 a 1x1 image, page 4 fine.
    const auto corrupt = load(firstPage("Broken", 0), {1280, 720});
    REQUIRE_FALSE(corrupt.ok());
    CHECK(corrupt.error().kind == snx::image::ImageErrorKind::Corrupt);
    CHECK_FALSE(load(firstPage("Broken", 1), {1280, 720}).ok());
    const auto tiny = load(firstPage("Broken", 2), {1280, 720});
    REQUIRE(tiny.ok());
    CHECK(tiny.value()->size() == snx::core::ImageSize{1, 1});
    CHECK(load(firstPage("Broken", 3), {1280, 720}).ok());
}

TEST_CASE("progress round trip: page 5 reads back as 5; beyond the end the server caps it at the page count") {
    Live live;
    const int alpha = libraryId(live, "Alpha");
    const auto chapters = must(live->chapters(alpha, 0, CancelToken()));
    REQUIRE(chapters.nodes.size() == 3);
    const int chapterId = chapters.nodes[1].id;  // Alpha chapter 2
    // The server learns the page count when the chapter is opened; before that it would save 0.
    REQUIRE(must(live->pages(chapterId, PageFormat::AsStored, CancelToken())).pages.size() == 10);

    const snx::core::ReadProgress saved = must(live->setProgress(chapterId, 5, false, CancelToken()));
    CHECK(saved.lastPageRead == 5);
    CHECK(saved.pageCount == 10);
    CHECK_FALSE(saved.isRead);
    const auto again = must(live->chapters(alpha, 0, CancelToken()));
    const auto it = std::find_if(again.nodes.begin(), again.nodes.end(),
                                 [chapterId](const snx::core::Chapter& c) { return c.id == chapterId; });
    REQUIRE(it != again.nodes.end());
    CHECK(it->lastPageRead == 5);

    CHECK(must(live->setProgress(chapterId, 99, false, CancelToken())).lastPageRead == 10);
    CHECK(must(live->setProgress(chapterId, -1, false, CancelToken())).lastPageRead == 0);  // back to the start
}

TEST_CASE("errors from the real server: unknown id, wrong sub-path, nobody listening") {
    Live live;
    CHECK(mustFail(live->manga(999999, CancelToken())).kind == ApiErrorKind::Server);

    ServerSettings wrongPath = serverSettings();
    wrongPath.url += "/not-here";
    Live lost(wrongPath);
    const ApiError notFound = mustFail(lost->serverInfo(CancelToken()));
    CHECK(notFound.kind == ApiErrorKind::Server);
    CHECK(notFound.detail.rfind("HTTP 404", 0) == 0);

    ServerSettings nobody = serverSettings();
    nobody.url = "http://127.0.0.1:1";
    Live off(nobody);
    CHECK(mustFail(off->serverInfo(CancelToken())).kind == ApiErrorKind::Offline);
}

}  // TEST_SUITE("all")

TEST_SUITE("basic") {

TEST_CASE("basic: a wrong password is Auth (HTTP 401); an image without the header is refused") {
    ServerSettings wrong = serverSettings();
    wrong.password = "wrong";
    Live live(wrong);
    const ApiError error = mustFail(live->categories(CancelToken()));
    CHECK(error.kind == ApiErrorKind::Auth);
    CHECK(error.detail.rfind("HTTP 401", 0) == 0);

    // Straight through a plain HttpClient (no SuwayomiClient, so no decorator): the server wants the login for images too.
    snx::net::CurlHttpClient plain(snx::net::curlOptionsFrom(snx::core::RuntimeProfile{}));
    snx::net::Request request;
    request.url = live->absoluteUrl("/api/v1/manga/1/thumbnail");
    auto answer = plain.submit(request, snx::net::Priority::Reader, CancelToken());
    REQUIRE(answer.waitFor(30s));
    REQUIRE(answer.get().ok());
    CHECK(answer.get().value().status == 401);
}

}  // TEST_SUITE("basic")

TEST_SUITE("ui_login") {

TEST_CASE("ui_login: no token yet, so the first call logs in with the saved password by itself") {
    Live live;
    CHECK(must(live->categories(CancelToken())).size() == kCategories);
    CHECK(live.logins.load() == 1);
    CHECK_FALSE(live->settings().accessToken.empty());
    CHECK_FALSE(live->settings().refreshToken.empty());
}

TEST_CASE("ui_login: login() and a wrong password") {
    Live live;
    const snx::core::LoginTokens tokens = must(live->login(env("SNX_USER"), env("SNX_PASS"), CancelToken()));
    CHECK_FALSE(tokens.accessToken.empty());
    CHECK_FALSE(tokens.refreshToken.empty());
    // Logging in again while holding a token works too: login goes out without it.
    must(live->login(env("SNX_USER"), env("SNX_PASS"), CancelToken()));

    Live other;
    const ApiError wrong = mustFail(other->login(env("SNX_USER"), "wrong", CancelToken()));
    CHECK(wrong.kind == ApiErrorKind::Auth);
    CHECK(wrong.message == "Incorrect username or password.");
}

TEST_CASE("ui_login: tokens refused and no password saved: Auth; logout ends the login") {
    ServerSettings settings = serverSettings();
    settings.password.clear();
    settings.accessToken = "not-a-token";
    settings.refreshToken = "not-a-token-either";
    Live live(settings);
    const ApiError error = mustFail(live->categories(CancelToken()));
    CHECK(error.kind == ApiErrorKind::Auth);
    CHECK(live.refreshes.load() == 1);
    CHECK(live.logins.load() == 0);

    Live loggedIn;
    must(loggedIn->login(env("SNX_USER"), env("SNX_PASS"), CancelToken()));
    loggedIn->logout();
    CHECK(mustFail(loggedIn->categories(CancelToken())).kind == ApiErrorKind::Auth);
}

TEST_CASE("ui_login: after the access token expired (20 s), calls and images still work, with one refresh each") {
    // Three clients, each logged in now; all their tokens expire together.
    Live single;
    Live burst;
    Live images;
    for (Live* live : {&single, &burst, &images}) {
        must((*live)->login(env("SNX_USER"), env("SNX_PASS"), CancelToken()));
        CHECK(must((*live)->categories(CancelToken())).size() == kCategories);
    }
    const std::string thumbnail = must(images->manga(libraryId(images, "Alpha"), CancelToken())).thumbnailUrl;
    CHECK(images.get(thumbnail).value().status == 200);
    MESSAGE("waiting 22 s for the test server's 20 s access tokens to expire");
    std::this_thread::sleep_for(22s);

    // One call: refused, refreshed, repeated.
    CHECK(must(single->categories(CancelToken())).size() == kCategories);
    CHECK(single.refreshes.load() == 1);

    // Five calls at once from five threads: one refresh between them.
    std::vector<std::thread> threads;
    std::atomic<int> ok{0};
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&] {
            auto future = burst->categories(CancelToken());
            if (future.waitFor(60s) && future.get().ok()) ok++;
        });
    }
    for (std::thread& thread : threads) thread.join();
    CHECK(ok.load() == 5);
    CHECK(burst.refreshes.load() == 1);

    // An image with the expired token is refused once; that starts a renewal, and the next try works.
    CHECK(images.get(thumbnail).value().status == 401);
    int status = 0;
    for (int i = 0; i < 30 && status != 200; ++i) {
        std::this_thread::sleep_for(100ms);
        status = images.get(thumbnail).value().status;
    }
    CHECK(status == 200);
    CHECK(images.refreshes.load() == 1);
}

}  // TEST_SUITE("ui_login")
