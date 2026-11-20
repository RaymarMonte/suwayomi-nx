#include <doctest/doctest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ApiTestKit.hpp"
#include "MainQueue.hpp"
#include "api/SuwayomiClient.hpp"
#include "core/Offline.hpp"
#include "core/ProgressQueue.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/UpdatesHistory.hpp"
#include "image/ImageLoader.hpp"
#include "net/CurlHttpClient.hpp"
#include "platform/SafeFs.hpp"
#include "ui/downloads/DownloadStore.hpp"
#include "ui/downloads/SwitchDownloads.hpp"

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

    // deliver: where the client's answers go (WP-18's downloader: a MainQueue the test thread runs); empty = any thread.
    explicit Live(ServerSettings settings = serverSettings(), std::function<void(std::function<void()>)> deliver = {}) {
        http.addResponseObserver([this](const snx::net::Request& sent, const snx::net::HttpResult&) {
            const std::string operation = operationOf(sent.body);
            if (operation == "RefreshToken") refreshes++;
            if (operation == "Login") logins++;
        });
        ClientOptions options = clientOptionsFrom(profile);
        if (deliver) {
            options.deliver = std::move(deliver);
        }
        client = std::make_unique<SuwayomiClient>(http, std::move(settings), options);
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
    CHECK(titles(reading) == std::vector<std::string>{"Alpha", "Beta RTL", "Epsilon Weekly"});
    CHECK(reading.totalCount == 3);
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
        loader.load(snx::image::LoadRequest{url, box, snx::net::Priority::Interactive, snx::image::ImageKind::Page, {}},
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

TEST_CASE("chapter actions (WP-10): updateChapters marks and unmarks two chapters; refreshManga") {
    Live live;
    const int delta = libraryId(live, "Delta Big");
    const auto page = must(live->chapters(delta, 0, CancelToken()));  // newest first: 300, 299, ...
    REQUIRE(page.nodes.size() >= 2);
    const std::vector<int> ids = {page.nodes[0].id, page.nodes[1].id};

    snx::core::ChapterPatch mark;
    mark.isRead = true;
    mark.isBookmarked = true;
    const auto marked = must(live->updateChapters(ids, mark, CancelToken()));
    REQUIRE(marked.size() == 2);
    for (const auto& chapter : marked) {
        CHECK(chapter.isRead);
        CHECK(chapter.isBookmarked);
        MESSAGE("lastReadAt after marking read: " << chapter.lastReadAt);  // D43: does marking stamp it?
    }
    CHECK(must(live->chapters(delta, 0, CancelToken())).nodes[0].isRead);

    snx::core::ChapterPatch undo;
    undo.isRead = false;
    undo.isBookmarked = false;
    undo.lastPageRead = 0;
    for (const auto& chapter : must(live->updateChapters(ids, undo, CancelToken()))) {
        CHECK_FALSE(chapter.isRead);
        CHECK_FALSE(chapter.isBookmarked);
        CHECK(chapter.lastPageRead == 0);
    }
    CHECK(must(live->updateChapters({}, undo, CancelToken())).empty());  // nothing asked

    // Local Source reads Broken's folder again: same manga, same single chapter.
    const snx::core::Manga refreshed = must(live->refreshManga(libraryId(live, "Broken"), CancelToken()));
    CHECK(refreshed.title == "Broken");
    CHECK(must(live->chapters(refreshed.id, 0, CancelToken())).totalCount == 1);
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

TEST_CASE("chapterState (WP-15b): the waiting-save queue's question reads what a save left; a gone chapter is Server") {
    Live live;
    const int alpha = libraryId(live, "Alpha");
    const auto chapters = must(live->chapters(alpha, 0, CancelToken()));
    REQUIRE(chapters.nodes.size() == 3);
    const int chapterId = chapters.nodes[0].id;  // Alpha chapter 3
    REQUIRE(must(live->pages(chapterId, PageFormat::AsStored, CancelToken())).pages.size() == 10);

    const snx::core::ReadProgress saved = must(live->setProgress(chapterId, 4, false, CancelToken()));
    CHECK(saved.lastReadAt > 0);  // any lastPageRead write stamps it
    const snx::core::ServerProgress state = must(live->chapterState(chapterId, CancelToken()));
    CHECK(state.page == 4);
    CHECK_FALSE(state.read);
    CHECK(state.lastReadAt == saved.lastReadAt);  // the queue's "still at the base" test relies on this
    CHECK(state.spotNote.empty());

    CHECK(mustFail(live->chapterState(999999, CancelToken())).kind == ApiErrorKind::Server);
    CHECK(must(live->setProgress(chapterId, 0, false, CancelToken())).lastPageRead == 0);  // back to the start

    // WP-18 (D59): a chapter read from the Switch's SD card learns its bookmark from the same question.
    CHECK_FALSE(state.bookmarked);
    snx::core::ChapterPatch bookmark;
    bookmark.isBookmarked = true;
    must(live->updateChapters({chapterId}, bookmark, CancelToken()));
    CHECK(must(live->chapterState(chapterId, CancelToken())).bookmarked);
    bookmark.isBookmarked = false;
    must(live->updateChapters({chapterId}, bookmark, CancelToken()));
    CHECK_FALSE(must(live->chapterState(chapterId, CancelToken())).bookmarked);
}

TEST_CASE("Updates and History (WP-15): newest first, a chapter with progress in History") {
    Live live;
    const auto updates = must(live->updates(0, CancelToken()));
    REQUIRE(updates.nodes.size() == 50);
    CHECK(updates.hasNextPage);
    for (std::size_t i = 1; i < updates.nodes.size(); ++i) {
        CHECK(updates.nodes[i - 1].chapter.fetchedAt >= updates.nodes[i].chapter.fetchedAt);
    }
    CHECK(updates.nodes[0].manga.inLibraryAt > 0);
    CHECK_FALSE(updates.nodes[0].manga.title.empty());

    // Alpha's chapter 1 at page 3: History's first row.
    const int alpha = libraryId(live, "Alpha");
    const auto chapters = must(live->chapters(alpha, 0, CancelToken()));
    REQUIRE(chapters.nodes.size() == 3);
    const int chapterId = chapters.nodes[2].id;  // newest first: chapter 1 is the last
    REQUIRE_FALSE(must(live->pages(chapterId, PageFormat::AsStored, CancelToken())).pages.empty());
    CHECK(must(live->setProgress(chapterId, 3, false, CancelToken())).lastPageRead == 3);
    const auto history = must(live->history(0, CancelToken()));
    REQUIRE_FALSE(history.nodes.empty());
    CHECK(history.nodes[0].chapter.id == chapterId);
    CHECK(history.nodes[0].chapter.lastPageRead == 3);
    CHECK(history.nodes[0].manga.title == "Alpha");
    CHECK(history.nodes[0].manga.inLibrary);
    // Back to the start: page 0 and not read is no history (the "read or with progress" part of the filter).
    CHECK(must(live->setProgress(chapterId, 0, false, CancelToken())).lastPageRead == 0);
    for (const snx::core::ChapterEntry& entry : must(live->history(0, CancelToken())).nodes) {
        CHECK(entry.chapter.id != chapterId);
    }
}

TEST_CASE("library update (WP-15): start, poll until done by the timestamp rule, then the summary") {
    Live live;
    const snx::core::LibraryUpdateStatus before = must(live->libraryUpdateStatus(false, CancelToken()));
    CHECK_FALSE(before.running);
    REQUIRE(must(live->startLibraryUpdate(CancelToken())));
    const auto started = std::chrono::steady_clock::now();
    auto elapsedMs = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    };
    snx::core::LibraryUpdateWatch watch = snx::core::LibraryUpdateWatch::started(before, 0, 10000, 1000);
    snx::core::UpdatePhase phase = snx::core::UpdatePhase::Starting;
    snx::core::LibraryUpdateStatus status;
    while (elapsedMs() < 20000) {
        status = must(live->libraryUpdateStatus(false, CancelToken()));
        phase = watch.next(status, elapsedMs());
        if (phase == snx::core::UpdatePhase::Done || phase == snx::core::UpdatePhase::NothingToUpdate) {
            break;
        }
        std::this_thread::sleep_for(200ms);
    }
    MESSAGE("library update: " << elapsedMs() << " ms, total " << status.total << ", skipped " << status.skippedManga);
    CHECK(phase == snx::core::UpdatePhase::Done);  // the timestamp moves even when every manga is skipped
    CHECK(status.lastUpdateMs != before.lastUpdateMs);
    CHECK(status.total + status.skippedManga >= 157);  // 7 fixture series + the Big shelf
    CHECK(must(live->libraryUpdateStatus(true, CancelToken())).failed.empty());
    CHECK(must(live->stopLibraryUpdate(CancelToken())));  // nothing runs: still fine
}

TEST_CASE("library tools (WP-24, D54): search with LIKE characters; a category's whole life; the server's own checks") {
    Live live;
    // A run that failed half-way may have left its test category: it goes first.
    for (const snx::core::Category& c : must(live->manageCategories(CancelToken()))) {
        if (c.name.rfind("WP-24", 0) == 0) must(live->deleteCategory(c.id, CancelToken()));
    }

    // Search: the whole library, any case; "%" and "_" are plain characters (escaped), not wildcards.
    CHECK(titles(must(live->searchLibrary("ALP", 0, CancelToken()))) == std::vector<std::string>{"Alpha"});
    CHECK(must(live->searchLibrary("a_p", 0, CancelToken())).nodes.empty());  // "_" as a wildcard would find "alp"
    CHECK(must(live->searchLibrary("%", 0, CancelToken())).nodes.empty());    // "%" as a wildcard would find all
    CHECK(titles(must(live->searchLibrary("日本語", 0, CancelToken()))) == std::vector<std::string>{"日本語タイトル テスト"});
    const auto shelf = must(live->searchLibrary("shelf", 0, CancelToken()));  // pages of libraryPageSize, by title
    CHECK(shelf.totalCount == 150);
    CHECK(shelf.hasNextPage);
    REQUIRE(shelf.nodes.size() == 60);
    CHECK(shelf.nodes.front().title == "Shelf 001");
    CHECK(must(live->searchLibrary("shelf", 2, CancelToken())).nodes.size() == 30);

    // The Categories screen's list: Default first, then the seed with each one's manga count.
    const std::vector<snx::core::Category> before = must(live->manageCategories(CancelToken()));
    REQUIRE(before.size() == kCategories);
    CHECK(before[0].isDefault);
    CHECK(before[1].name == "Reading");
    CHECK(before[1].mangaCount == 3);
    CHECK(before[3].mangaCount == 150);
    CHECK(before[4].mangaCount == 0);

    // The server refuses only an exact duplicate (the app checks the rest itself, D54).
    const ApiError duplicate = mustFail(live->createCategory("Reading", CancelToken()));
    CHECK(duplicate.kind == ApiErrorKind::Server);

    // Create -> rename -> move to the front -> Alpha into it -> delete: Alpha stays in the library, in Reading.
    const snx::core::Category made = must(live->createCategory("WP-24 test", CancelToken()));
    CHECK(made.name == "WP-24 test");
    CHECK(made.order == 5);
    const snx::core::Category renamed = must(live->renameCategory(made.id, "WP-24 renamed", CancelToken()));
    CHECK(renamed.id == made.id);
    CHECK(renamed.name == "WP-24 renamed");
    const std::vector<snx::core::Category> moved = must(live->moveCategory(made.id, 1, CancelToken()));
    std::vector<std::string> order;
    for (const auto& c : moved) {
        if (!c.isDefault) order.push_back(c.name);
    }
    CHECK(order == std::vector<std::string>{"WP-24 renamed", "Reading", "Long series", "Big shelf", "Empty"});

    const int alpha = libraryId(live, "Alpha");
    const std::vector<snx::core::Category> in = must(live->setMangaCategories(alpha, {made.id, 0}, {}, CancelToken()));
    std::vector<int> ids;
    for (const auto& c : in) ids.push_back(c.id);
    std::sort(ids.begin(), ids.end());
    CHECK(ids == std::vector<int>{before[1].id, made.id});  // Reading kept, the new one added, id 0 left out

    CHECK(must(live->deleteCategory(made.id, CancelToken())) == 1);  // it had one manga
    const std::vector<snx::core::Category> after = must(live->manageCategories(CancelToken()));
    REQUIRE(after.size() == kCategories);
    CHECK(after[1].name == "Reading");
    CHECK(after[1].order == 1);  // the server renumbers after a delete
    CHECK(titles(must(live->library(before[1].id, 0, CancelToken()))) ==
          std::vector<std::string>{"Alpha", "Beta RTL", "Epsilon Weekly"});
    const ApiError gone = mustFail(live->deleteCategory(made.id, CancelToken()));  // already gone: no category deleted
    CHECK(gone.kind == ApiErrorKind::Server);
}

namespace {

// WP-23's test source (TESTING.md "Test source"): SNX Test (EN), 60 manga, 25 per page.
const std::string kSnxEn = "1105946461371740061";
const std::string kSnxJa = "1294398940357362604";

std::vector<std::string> browseTitles(const snx::core::BrowsePage& page) {
    std::vector<std::string> out;
    for (const auto& manga : page.manga) out.push_back(manga.title);
    return out;
}

// The test source's switch file (tools/test-source Control.java reads it on every call). Written to a temporary file
// first and renamed, so the source never reads half of it. Empty text removes it.
void setSwitchFile(const std::string& text) {
    const std::string path = std::string(SNX_TEST_SITE_DIR) + "/control.json";
    if (text.empty()) {
        std::remove(path.c_str());
        return;
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
    }
    REQUIRE(std::rename(tmp.c_str(), path.c_str()) == 0);
}

int categoryId(Live& live, const std::string& name) {
    for (const snx::core::Category& c : must(live->categories(CancelToken()))) {
        if (c.name == name) return c.id;
    }
    FAIL("no category " << name);
    return 0;
}

}  // namespace

TEST_CASE("browse (WP-14, D56): sources, filters, paging, search, filters narrow, a source failure, add and remove") {
    using snx::core::gql::BrowseKind;
    Live live;
    setSwitchFile("");
    const nlohmann::json none = nlohmann::json::array();

    // The sources: Local and SNX Test's two (the seed installs only SNX Test).
    const std::vector<snx::core::Source> sources = must(live->sources(CancelToken()));
    std::vector<std::string> ids;
    for (const auto& s : sources) ids.push_back(s.id);
    std::sort(ids.begin(), ids.end());
    CHECK(ids == std::vector<std::string>{"0", kSnxEn, kSnxJa});
    for (const auto& s : sources) {
        if (s.id == "0") CHECK(s.lang == "localsourcelang");
        if (s.id == kSnxEn) {
            CHECK(s.displayName == "SNX Test (EN)");
            CHECK(s.supportsLatest);
            CHECK(s.contentWarning == snx::core::ContentWarning::Safe);
        }
    }

    // The filters as a tree: 8, two groups (7 genres, 3 types), the aliased defaults.
    const std::vector<snx::core::SourceFilter> filters = must(live->sourceFilters(kSnxEn, CancelToken()));
    REQUIRE(filters.size() == 8);
    CHECK(filters[2].kind == snx::core::FilterKind::Select);
    CHECK(filters[5].kind == snx::core::FilterKind::Group);
    CHECK(filters[5].children.size() == 7);
    CHECK(filters[6].children.size() == 3);
    REQUIRE(filters[7].defaults.sort.has_value());
    CHECK(filters[7].defaults.sort->index == 0);
    const std::vector<snx::core::SourceFilter> local = must(live->sourceFilters("0", CancelToken()));
    REQUIRE(local.size() == 1);
    CHECK(local[0].kind == snx::core::FilterKind::Sort);

    // Popular pages 25 + 25 + 10 (pages start at 1); the most popular is Comet Courier. Latest starts with Paper Garden.
    const auto page1 = must(live->browseSource(kSnxEn, BrowseKind::Popular, 1, "", none, CancelToken()));
    CHECK(page1.manga.size() == 25);
    CHECK(page1.hasNextPage);
    REQUIRE_FALSE(page1.manga.empty());
    CHECK(page1.manga[0].title == "Comet Courier");
    CHECK(must(live->browseSource(kSnxEn, BrowseKind::Popular, 2, "", none, CancelToken())).manga.size() == 25);
    const auto page3 = must(live->browseSource(kSnxEn, BrowseKind::Popular, 3, "", none, CancelToken()));
    CHECK(page3.manga.size() == 10);
    CHECK_FALSE(page3.hasNextPage);
    const auto latest = must(live->browseSource(kSnxEn, BrowseKind::Latest, 1, "", none, CancelToken()));
    REQUIRE_FALSE(latest.manga.empty());
    CHECK(latest.manga[0].title == "Paper Garden");

    // Search "comet": 3, by popularity.
    CHECK(browseTitles(must(live->browseSource(kSnxEn, BrowseKind::Search, 1, "comet", none, CancelToken()))) ==
          std::vector<std::string>{"Comet Courier", "Comet Over the Bay", "The Last Comet"});

    // One filter of each type narrows as the catalog says (tools/gen-fixtures/test-source.ts).
    auto changed = [&](auto edit) {
        snx::core::FilterSettings settings = snx::core::defaultSettings(filters);
        edit(settings);
        return snx::core::filterChanges(filters, settings);
    };
    auto search = [&](const std::string& query, const nlohmann::json& changes) {
        return must(live->browseSource(kSnxEn, BrowseKind::Search, 1, query, changes, CancelToken()));
    };
    CHECK(search("", changed([](auto& s) { s[1].value.text = "Aiko Mori"; })).manga.size() == 6);   // Text
    CHECK(search("", changed([](auto& s) { s[2].value.select = 2; })).manga.size() == 15);           // Select: Completed
    // CheckBox "In colour": 31 (every Manhwa and Webtoon, and the Manga numbered i % 7 == 0): 25 + 6.
    const nlohmann::json inColour = changed([](auto& s) { s[4].value.check = true; });
    CHECK(search("", inColour).hasNextPage);
    const auto colour2 = must(live->browseSource(kSnxEn, BrowseKind::Search, 2, "", inColour, CancelToken()));
    CHECK(colour2.manga.size() == 6);
    CHECK_FALSE(colour2.hasNextPage);
    CHECK(browseTitles(search("", changed([](auto& s) { s[5].children[6].tri = snx::core::TriState::Include; }))) ==
          std::vector<std::string>{"Skyline Strip"});                                                  // TriState
    CHECK(search("", changed([](auto& s) { s[6].children[2].check = true; })).manga.size() == 13);   // Group: Webtoon
    CHECK(browseTitles(search("comet", changed([](auto& s) { s[7].value.sort = snx::core::SortSelection{1, false}; }))) ==
          std::vector<std::string>{"The Last Comet", "Comet Over the Bay", "Comet Courier"});        // Sort: Title down

    // A source failure (the switch file) is a Server error with the source's own message.
    setSwitchFile(R"({"fail":[{"lang":"en","call":"search"}]})");
    const ApiError failed = mustFail(live->browseSource(kSnxEn, BrowseKind::Search, 1, "x", none, CancelToken()));
    setSwitchFile("");
    CHECK(failed.kind == ApiErrorKind::Server);
    CHECK(failed.detail == "SNX test failure: search");

    // Add Comet Courier with two categories: exactly those. The same-title check finds it; remove takes it out again.
    const int comet = page1.manga[0].id;
    const int reading = categoryId(live, "Reading");
    const int longSeries = categoryId(live, "Long series");
    const snx::core::Manga added = must(live->addToLibrary(comet, {reading, longSeries}, CancelToken()));
    CHECK(added.inLibrary);
    const snx::core::Manga details = must(live->manga(comet, CancelToken()));
    CHECK(details.inLibrary);
    std::vector<int> in;
    for (const auto& c : details.categories) in.push_back(c.id);
    std::sort(in.begin(), in.end());
    CHECK(in == std::vector<int>{reading, longSeries});
    const std::vector<snx::core::Manga> dupes = must(live->libraryDuplicates("COMET COURIER", 0, CancelToken()));
    REQUIRE(dupes.size() == 1);
    CHECK(dupes[0].id == comet);
    CHECK(must(live->libraryDuplicates("Comet Courier", comet, CancelToken())).empty());  // itself left out
    CHECK(must(live->libraryDuplicates("Comet_Courier", 0, CancelToken())).empty());      // "_" is no wildcard

    // Added again with one category: the old ones are cleared.
    CHECK(must(live->addToLibrary(comet, {reading}, CancelToken())).categories.size() == 1);
    CHECK_FALSE(must(live->removeFromLibrary(comet, CancelToken())).inLibrary);
    CHECK_FALSE(must(live->manga(comet, CancelToken())).inLibrary);
    must(live->setMangaCategories(comet, {}, {reading}, CancelToken()));  // as it was: no categories

    // "Tick for new manga": the category's default flag, set and cleared.
    CHECK(must(live->setCategoryDefault(longSeries, true, CancelToken())).isDefault);
    CHECK_FALSE(must(live->setCategoryDefault(longSeries, false, CancelToken())).isDefault);
}

TEST_CASE("server downloads (WP-17, D58): enqueue, status, reorder, remove, stop, start, clear; download, delete") {
    using namespace std::chrono_literals;
    using snx::core::gql::BrowseKind;
    Live live;
    setSwitchFile("");
    must(live->clearDownloads(CancelToken()));  // a known state: nothing queued
    // Paper Garden (SNX Test EN, the first Latest), its chapters fetched from the source. Reading order: oldest first.
    const auto latest = must(live->browseSource(kSnxEn, BrowseKind::Latest, 1, "", nlohmann::json::array(), CancelToken()));
    REQUIRE_FALSE(latest.manga.empty());
    const int mangaId = latest.manga[0].id;
    must(live->refreshManga(mangaId, CancelToken()));
    std::vector<snx::core::Chapter> chapters = must(live->chapters(mangaId, 0, CancelToken())).nodes;
    REQUIRE(chapters.size() >= 4);
    std::reverse(chapters.begin(), chapters.end());
    const int a = chapters[0].id, b = chapters[1].id, c = chapters[2].id, d = chapters[3].id;
    const auto ids = [](const snx::core::DownloadQueue& q) {
        std::vector<int> out;
        for (const auto& row : q.rows) out.push_back(row.chapterId);
        return out;
    };
    const auto has = [&](const snx::core::DownloadQueue& q, int id) {
        const std::vector<int> all = ids(q);
        return std::find(all.begin(), all.end(), id) != all.end();
    };

    // Slowed down (each source call waits 1.5 s), so the queue stands still long enough to look at.
    setSwitchFile(R"({"delayMs":1500})");
    const auto queued = must(live->enqueueDownloads({a, b, c, d}, CancelToken()));
    CHECK(ids(queued) == std::vector<int>{a, b, c, d});
    CHECK_FALSE(queued.started);  // the enqueue's own answer: the downloader starts by itself a moment later
    CHECK(queued.rows[0].mangaTitle == "Paper Garden");
    CHECK(ids(must(live->downloadStatus(CancelToken()))) == std::vector<int>{a, b, c, d});
    CHECK(ids(must(live->enqueueDownloads({b}, CancelToken()))).size() == 4);  // not added twice
    CHECK(ids(must(live->reorderDownload(d, 0, CancelToken())))[0] == d);   // top = place 0 of the whole queue
    CHECK(ids(must(live->reorderDownload(d, 3, CancelToken())))[3] == d);   // bottom = rows - 1
    CHECK_FALSE(has(must(live->dequeueDownload(c, CancelToken())), c));
    CHECK_FALSE(must(live->stopDownloads(CancelToken())).started);
    CHECK(must(live->startDownloads(CancelToken())).started);  // waits until STARTED (about a second)
    CHECK(must(live->clearDownloads(CancelToken())).total() == 0);

    // At full speed: a downloads, leaves the queue, and the chapter says isDownloaded; delete takes it away again.
    setSwitchFile("");
    must(live->enqueueDownloads({a}, CancelToken()));
    bool left = false;
    for (int i = 0; i < 120 && !left; ++i) {
        std::this_thread::sleep_for(250ms);
        left = !has(must(live->downloadStatus(CancelToken())), a);
    }
    REQUIRE(left);
    const auto isDownloaded = [&](int id) {
        for (const auto& chapter : must(live->chapters(mangaId, 0, CancelToken())).nodes) {
            if (chapter.id == id) return chapter.isDownloaded;
        }
        return false;
    };
    CHECK(isDownloaded(a));
    CHECK(must(live->chapterState(a, CancelToken())).downloaded);  // WP-18: the same flag through ChapterState
    const std::vector<snx::core::Chapter> deleted = must(live->deleteServerDownloads({a, b}, CancelToken()));
    REQUIRE(deleted.size() == 2);
    CHECK_FALSE(deleted[0].isDownloaded);
    CHECK_FALSE(isDownloaded(a));
    CHECK_FALSE(must(live->chapterState(a, CancelToken())).downloaded);
    CHECK(must(live->deleteServerDownloads({999999}, CancelToken())).empty());  // an unknown id: harmless
    must(live->clearDownloads(CancelToken()));
}

// WP-18 (D59): the Switch downloader with the real client, into a temp folder (a SafeFs of its own). The test thread
// plays the main thread: the client's answers, the store's and the model's posts run here, and so do the model's timers.
struct SwitchRig {
    std::string parent;
    MainQueue main;
    Live live{serverSettings(), main.deliver()};
    std::unique_ptr<snx::platform::SafeFs> fs;
    std::unique_ptr<snx::ui::DownloadStore> store;
    std::unique_ptr<snx::ui::SwitchDownloads> model;
    std::vector<std::pair<std::chrono::steady_clock::time_point, std::function<void()>>> timers;

    SwitchRig() {
        char pattern[] = "/tmp/snx-integration-switchdl-XXXXXX";
        const char* made = ::mkdtemp(pattern);
        REQUIRE(made != nullptr);
        parent = std::string(made) + "/";
        REQUIRE(::mkdir((parent + "data/").c_str(), 0755) == 0);
        fs = std::make_unique<snx::platform::SafeFs>(parent + "data/");
        snx::ui::DownloadStore::Options options = snx::ui::DownloadStore::optionsFrom(live.profile);
        options.freeBytes = [] { return std::optional<std::uint64_t>(100ULL * snx::core::kGiB); };
        store = std::make_unique<snx::ui::DownloadStore>(*fs, options, main.deliver());
        snx::ui::SwitchDownloads::Hooks hooks;
        hooks.clockMs = [] {
            return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::steady_clock::now().time_since_epoch())
                                                 .count());
        };
        hooks.later = [this](int ms, std::function<void()> fn) {
            timers.emplace_back(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms), std::move(fn));
        };
        hooks.post = main.deliver();
        hooks.nowSeconds = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
        hooks.memoryLow = [] { return false; };
        model = std::make_unique<snx::ui::SwitchDownloads>(*live.client, live.http, *store,
                                                           snx::ui::SwitchDownloads::limitsFrom(live.profile), hooks);
        model->setLimit(0);
        model->setServer(env("SNX_SERVER_URL"));
        REQUIRE(drive([this] { return model->loaded(); }, 10s));
        model->connected();
    }
    ~SwitchRig() {
        model->shutdown();
        main.settle(100);
        model.reset();
        store.reset();
        snx::platform::SafeFs(parent).removeTree("data");
        ::rmdir(parent.c_str());
    }

    // Runs the main thread's work and the due timers until done() (false after `limit`).
    bool drive(const std::function<bool()>& done, std::chrono::seconds limit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            main.settle(20);
            const auto now = std::chrono::steady_clock::now();
            std::vector<std::function<void()>> due;
            for (auto it = timers.begin(); it != timers.end();) {
                if (it->first <= now) {
                    due.push_back(std::move(it->second));
                    it = timers.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto& fn : due) fn();
        }
        return true;
    }

    // must() for a call whose answer comes through the main queue.
    template <typename T>
    T mustOnMain(ApiFuture<T> future) {
        REQUIRE(drive([&] { return future.waitFor(0ms); }, 60s));
        const ApiResult<T>& result = future.get();
        REQUIRE_MESSAGE(result.ok(), apiErrorKindName(result.error().kind) << ": " << result.error().detail);
        return result.value();
    }

    // Queues the chapter and waits until it is on the Switch or failed.
    snx::ui::SwitchDownloads::ChapterStatus download(int mangaId, const snx::core::Chapter& chapter) {
        using State = snx::ui::SwitchDownloads::ChapterState;
        const snx::core::Manga manga = mustOnMain(live->manga(mangaId, CancelToken()));
        REQUIRE(model->enqueue(manga, {chapter}) == 1);
        REQUIRE(drive(
            [&] {
                const State state = model->chapterState(chapter.id).state;
                return state == State::OnSwitch || state == State::Failed;
            },
            90s));
        return model->chapterState(chapter.id);
    }

    // WP-18c: the server's own page cache emptied (SUWAYOMI-API.md), so pages come from the source again (after the switch
    // file's delay). A raw GraphQL POST through the same HttpClient: the client's decorator adds the login.
    void clearPageCache() {
        snx::net::Request request;
        request.method = snx::net::Method::Post;
        request.url = live->absoluteUrl("/api/graphql");
        request.headers = {{"Content-Type", "application/json"}};
        request.body = R"({"query":"mutation { clearCachedImages(input: { cachedPages: true }) { cachedPages } }"})";
        auto answer = live.http.submit(request, snx::net::Priority::Reader, CancelToken());
        REQUIRE(answer.waitFor(30s));
        REQUIRE(answer.get().ok());
        REQUIRE_MESSAGE(answer.get().value().status == 200, answer.get().value().body);
    }

    // Every page file of the chapter equals the server's page, byte for byte (the page GET waits on its own: the
    // HttpClient's answers don't go through the main queue).
    void checkPagesEqualServer(int chapterId) {
        const auto files = model->pageFiles(chapterId);
        REQUIRE(files.has_value());
        const snx::core::PageList pages = mustOnMain(live->pages(chapterId, PageFormat::AsStored, CancelToken()));
        REQUIRE(files->size() == pages.pages.size());
        for (std::size_t i = 0; i < pages.pages.size(); ++i) {
            const auto bytes = fs->readFile((*files)[i], 64 * 1024 * 1024);
            REQUIRE_MESSAGE(bytes.has_value(), "page " << i << ": " << (*files)[i]);
            const snx::net::HttpResult page = live.get(pages.pages[i]);
            REQUIRE(page.ok());
            CHECK_MESSAGE(*bytes == page.value().body, "page " << i);
        }
    }
};

TEST_CASE("Switch downloads (WP-18, D59): Alpha (Local) and Comet Courier (SNX Test) onto a temp SD card equal the server's pages") {
    using State = snx::ui::SwitchDownloads::ChapterState;
    setSwitchFile("");
    SwitchRig rig;
    // Alpha (Local): chapter 1 = the last of the list (newest first). (libraryId() can't be used here: this client's
    // answers come through the main queue, which only mustOnMain runs.)
    int alpha = 0;
    for (const int category : {0, 1, 2}) {
        for (const snx::core::Manga& manga : rig.mustOnMain(rig.live->library(category, 0, CancelToken())).nodes) {
            if (manga.title == "Alpha") alpha = manga.id;
        }
    }
    REQUIRE(alpha > 0);
    const auto alphaChapters = rig.mustOnMain(rig.live->chapters(alpha, 0, CancelToken())).nodes;
    REQUIRE_FALSE(alphaChapters.empty());
    const auto t0 = std::chrono::steady_clock::now();
    const auto local = rig.download(alpha, alphaChapters.back());
    const auto localMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    REQUIRE(local.state == State::OnSwitch);
    MESSAGE("Alpha chapter 1 (" << local.pages << " pages) downloaded in " << localMs << " ms");
    rig.checkPagesEqualServer(alphaChapters.back().id);

    // Comet Courier (SNX Test EN, its own JPEG pages), its chapters fetched from the source first. Not Paper Garden: the
    // server keeps the pages it served in its own cache, and the server-downloads e2e needs Paper Garden's pages slow.
    const auto comets = rig.mustOnMain(rig.live->browseSource(kSnxEn, snx::core::gql::BrowseKind::Search, 1, "Comet Courier",
                                                              nlohmann::json::array(), CancelToken()));
    int comet = 0;
    for (const auto& manga : comets.manga) {
        if (manga.title == "Comet Courier") comet = manga.id;
    }
    REQUIRE(comet > 0);
    rig.mustOnMain(rig.live->refreshManga(comet, CancelToken()));
    const auto cometChapters = rig.mustOnMain(rig.live->chapters(comet, 0, CancelToken())).nodes;
    REQUIRE_FALSE(cometChapters.empty());
    REQUIRE(rig.download(comet, cometChapters.back()).state == State::OnSwitch);
    rig.checkPagesEqualServer(cometChapters.back().id);

    // Missing Page Mansion: chapter 1's page 2 answers 404 → Failed at page 2 (index 1), nothing of it on the Switch.
    const auto found = rig.mustOnMain(rig.live->browseSource(kSnxEn, snx::core::gql::BrowseKind::Search, 1,
                                                             "Missing Page Mansion", nlohmann::json::array(),
                                                             CancelToken()));
    REQUIRE_FALSE(found.manga.empty());
    const int mansion = found.manga[0].id;
    rig.mustOnMain(rig.live->refreshManga(mansion, CancelToken()));
    const auto mansionChapters = rig.mustOnMain(rig.live->chapters(mansion, 0, CancelToken())).nodes;
    REQUIRE_FALSE(mansionChapters.empty());
    const auto failed = rig.download(mansion, mansionChapters.back());
    CHECK(failed.state == State::Failed);
    CHECK(failed.failedPage == 1);
    CHECK_FALSE(rig.model->pageFiles(mansionChapters.back().id).has_value());

    // Delete: the manga go from the model's view (the folders on the store's thread).
    rig.model->deleteManga(alpha);
    rig.model->deleteManga(comet);
    rig.model->clear();
    REQUIRE(rig.drive([&] { return rig.model->downloadedManga().empty(); }, 10s));
}

// Here for SwitchRig (a suite block is a namespace of its own), but in the ui_login suite: it runs in that mode only.
TEST_CASE("ui_login (WP-18c): a Switch download across the 20 s token expiry renews the login and finishes" *
          doctest::test_suite("ui_login")) {
    using State = snx::ui::SwitchDownloads::ChapterState;
    setSwitchFile("");
    SwitchRig rig;
    // Glass Harbor (SNX Test en-09, 3 pages a chapter): no other test reads its pages. The rig's first call logs in, so the
    // test server's 20 s access token starts about now.
    const auto found = rig.mustOnMain(rig.live->browseSource(kSnxEn, snx::core::gql::BrowseKind::Search, 1, "Glass Harbor",
                                                             nlohmann::json::array(), CancelToken()));
    const auto loggedIn = std::chrono::steady_clock::now();
    CHECK(rig.live.logins.load() == 1);
    int glass = 0;
    for (const auto& manga : found.manga) {
        if (manga.title == "Glass Harbor") glass = manga.id;
    }
    REQUIRE(glass > 0);
    rig.mustOnMain(rig.live->refreshManga(glass, CancelToken()));
    const auto chapters = rig.mustOnMain(rig.live->chapters(glass, 0, CancelToken())).nodes;
    REQUIRE_FALSE(chapters.empty());
    rig.clearPageCache();
    // Started 14 s after the login with every source call 8 s slow: the cover and the page list go out with the token
    // still good (answered at ~22 s), the pages after it ran out (a 401, one renewal, the page again).
    std::this_thread::sleep_until(loggedIn + 14s);
    setSwitchFile(R"({"delayMs":8000})");
    const auto status = rig.download(glass, chapters.back());
    setSwitchFile("");
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - loggedIn).count();
    MESSAGE("Glass Harbor chapter 1 (" << status.pages << " pages) on the Switch " << ms << " ms after the login, "
                                       << rig.live.refreshes.load() << " refresh(es)");
    REQUIRE(status.state == State::OnSwitch);
    CHECK(ms > 20000);
    CHECK(rig.live.refreshes.load() >= 1);  // renewed with the refresh token ...
    CHECK(rig.live.logins.load() == 1);     // ... never a new login with the password
    rig.checkPagesEqualServer(chapters.back().id);
    rig.model->deleteManga(glass);
    REQUIRE(rig.drive([&] { return rig.model->downloadedManga().empty(); }, 10s));
}

TEST_CASE("offline sync (WP-18b, D61): DownloadedProgress answers many chapters at once and leaves out a gone id; the Date "
          "header gives a clock offset near 0; a waiting save decided against the server's real stamps") {
    Live live;
    const int alpha = libraryId(live, "Alpha");
    const auto chapters = must(live->chapters(alpha, 0, CancelToken()));
    REQUIRE(chapters.nodes.size() == 3);
    const int chapterId = chapters.nodes[2].id;  // Alpha chapter 1
    REQUIRE(must(live->pages(chapterId, PageFormat::AsStored, CancelToken())).pages.size() == 10);
    const snx::core::ReadProgress saved = must(live->setProgress(chapterId, 6, false, CancelToken()));
    REQUIRE(saved.lastReadAt > 0);

    std::vector<int> ids;
    for (const snx::core::Chapter& c : chapters.nodes) {
        ids.push_back(c.id);
    }
    ids.push_back(999999);  // a chapter the server doesn't have: "gone"
    const auto progress = must(live->downloadedProgress(ids, CancelToken()));
    CHECK(progress.size() == 3);
    CHECK(progress.count(999999) == 0);
    REQUIRE(progress.count(chapterId) == 1);
    CHECK(progress.at(chapterId).page == 6);
    CHECK(progress.at(chapterId).lastReadAt == saved.lastReadAt);  // the same stamp chapterState reads

    // The server's clock: its Date header against this PC's (the same machine: within a few seconds).
    const snx::net::HttpResult answer = live.get("/api/graphql");
    REQUIRE(answer.ok());
    const std::optional<std::int64_t> serverNow = snx::core::parseHttpDate(answer.value().header("date"));
    REQUIRE(serverNow.has_value());
    CHECK(std::llabs(*serverNow - static_cast<std::int64_t>(std::time(nullptr))) <= 5);

    // A waiting save made from page 0 while the server moved on to page 6: made before the server's stamp it is dropped
    // ("server later"), made after it it is sent ("later"); read here while the server's isn't: sent ("finished").
    snx::core::WaitingSave entry;
    entry.chapterId = chapterId;
    entry.page = 2;
    entry.madeAt = saved.lastReadAt - 60;
    const snx::core::ServerProgress& server = progress.at(chapterId);
    CHECK(snx::core::decideWaitingSave(entry, server, 0) == snx::core::WaitingDecision::ServerLater);
    entry.madeAt = saved.lastReadAt + 60;
    CHECK(snx::core::decideWaitingSave(entry, server, 0) == snx::core::WaitingDecision::Later);
    entry.madeAt = saved.lastReadAt - 60;
    entry.read = true;
    CHECK(snx::core::decideWaitingSave(entry, server, 0) == snx::core::WaitingDecision::Finished);

    CHECK(must(live->setProgress(chapterId, 0, false, CancelToken())).lastPageRead == 0);  // back to the start
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
