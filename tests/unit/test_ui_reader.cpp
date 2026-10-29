// WP-11: the paged reader's model (src/ui/reader/ReaderModel) on a FakeHttpClient with a real ImageLoader: opening a chapter,
// the prefetch window (and letting go of pages outside it), turning by side and direction, the end screens and moving to the
// next / previous chapter (the next one's list and first pages fetched ahead), broken pages, the fit, and the progress sent to
// the server (at most every 5 s, the last page marks the chapter read, everything left when closing).
// The client answers inside the call (no deliver); the loader's answers come through MainQueue (the test plays the main thread).
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "ApiTestKit.hpp"
#include "Fixtures.hpp"
#include "MainQueue.hpp"
#include "api/SuwayomiClient.hpp"
#include "core/RuntimeProfile.hpp"
#include "image/ImageLoader.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/reader/ReaderModel.hpp"

using namespace snx;
using core::ReadingDirection;
using core::Side;
using net::FakeHttpClient;
using net::Request;
using ui::ReaderModel;
using ui::ReaderPage;
using PageState = ui::ReaderPage::State;
using State = ui::ReaderModel::State;

namespace {

constexpr int kMangaId = 1;

struct FakeChapter {
    int id = 0;
    int sourceOrder = 0;
    int pages = 10;
    bool isRead = false;
    int lastPageRead = 0;
};

// The server: three chapters (ids 101..103, 10, 10 and 4 pages), page images from the fixtures.
struct FakeServer {
    std::vector<FakeChapter> chapters{{101, 1, 10}, {102, 2, 10}, {103, 3, 4}};
    std::set<std::string> brokenPages;  // "<sourceOrder>/<page>": answered with a corrupt JPEG
    std::set<int> emptyPaths;           // page indexes listed as "" (a form the reader can't use)
    bool failPages = false;
    bool failProgress = false;
    bool offline = false;  // no answer at all, to anything (the server stopped, WP-12)
    std::vector<nlohmann::json> progress;  // every UpdateChapterProgress patch, with "id"
    std::string image = readFixture("images/alpha-p1.jpg");
    std::string corrupt = readFixture("images/broken-corrupt.jpg");

    FakeChapter* find(int id) {
        for (FakeChapter& c : chapters) {
            if (c.id == id) {
                return &c;
            }
        }
        return nullptr;
    }

    net::HttpResult answer(const Request& request) {
        if (offline) {
            return net::HttpResult::fail(core::ErrorKind::Network, "Connection refused");
        }
        const std::string prefix = "http://s/api/v1/manga/1/chapter/";
        if (request.url.rfind(prefix, 0) == 0) {
            const std::string rest = request.url.substr(prefix.size());  // "<sourceOrder>/page/<n>"
            const std::string key = rest.substr(0, rest.find('/')) + "/" + rest.substr(rest.rfind('/') + 1);
            return FakeHttpClient::answer(200, brokenPages.count(key) ? corrupt : image);
        }
        const std::string operation = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (operation == "FetchChapterPages") {
            FakeChapter* c = find(vars["chapterId"].get<int>());
            if (failPages || c == nullptr) {
                return FakeHttpClient::answer(500, "oops");
            }
            nlohmann::json pages = nlohmann::json::array();
            for (int i = 0; i < c->pages; ++i) {
                pages.push_back(emptyPaths.count(i) ? std::string() :
                    "/api/v1/manga/1/chapter/" + std::to_string(c->sourceOrder) + "/page/" + std::to_string(i));
            }
            return FakeHttpClient::answer(200, nlohmann::json{{"data", {{"fetchChapterPages", {
                {"pages", pages},
                {"chapter", {{"id", c->id}, {"mangaId", kMangaId}, {"sourceOrder", c->sourceOrder}, {"isRead", c->isRead},
                             {"lastPageRead", c->lastPageRead}, {"pageCount", c->pages}}},
                {"syncConflict", nullptr}}}}}}.dump());
        }
        if (operation == "UpdateChapterProgress") {
            if (failProgress) {
                return FakeHttpClient::answer(500, "oops");
            }
            FakeChapter* c = find(vars["id"].get<int>());
            REQUIRE(c != nullptr);
            nlohmann::json patch = vars["patch"];
            patch["id"] = c->id;
            progress.push_back(patch);
            c->lastPageRead = patch["lastPageRead"].get<int>();
            if (patch.contains("isRead") && patch["isRead"].get<bool>()) {
                c->isRead = true;
            }
            return FakeHttpClient::answer(200, nlohmann::json{{"data", {{"updateChapter", {{"chapter", {
                {"id", c->id}, {"lastPageRead", c->lastPageRead}, {"isRead", c->isRead}, {"pageCount", c->pages}}}}}}}}.dump());
        }
        return FakeHttpClient::answer(404);
    }
};

struct Harness {
    FakeServer server;
    FakeHttpClient http;
    MainQueue main;
    std::unique_ptr<api::SuwayomiClient> client;
    std::unique_ptr<image::ImageLoader> images;
    std::int64_t now = 1000;
    int changes = 0;
    std::vector<std::pair<core::ReadProgress, bool>> saved;  // onSaved

    Harness() {
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = "http://s";
        client = std::make_unique<api::SuwayomiClient>(http, settings, api::ClientOptions{});
        const core::RuntimeProfile profile =
            core::makeRuntimeProfile(core::LaunchMode::Application, 4 * core::kGiB, 100 * core::kMiB);
        image::LoaderOptions options = image::loaderOptionsFrom(profile);
        options.deliver = main.deliver();
        images = std::make_unique<image::ImageLoader>(http, options);
    }

    std::vector<core::Chapter> chapters() const {
        std::vector<core::Chapter> list;
        for (auto it = server.chapters.rbegin(); it != server.chapters.rend(); ++it) {  // newest first, like MangaModel
            core::Chapter c;
            c.id = it->id;
            c.mangaId = kMangaId;
            c.name = "Chapter " + std::to_string(it->sourceOrder);
            c.sourceOrder = it->sourceOrder;
            c.isRead = it->isRead;
            c.lastPageRead = it->lastPageRead;
            c.pageCount = it->pages;
            list.push_back(c);
        }
        return list;
    }

    std::unique_ptr<ReaderModel> model(ReadingDirection direction = ReadingDirection::LeftToRight, int behind = 1,
                                       int ahead = 3) {
        core::Manga manga;
        manga.id = kMangaId;
        manga.title = "Alpha";
        ui::ReaderOptions options;
        options.prefetchBehind = behind;
        options.prefetchAhead = ahead;
        options.clock = [this] { return now; };
        options.onSaved = [this](const core::ReadProgress& p, bool ok) { saved.emplace_back(p, ok); };
        core::MangaReaderSettings settings;
        settings.direction = direction;
        auto made = std::make_unique<ReaderModel>(*client, *images, manga, chapters(), settings, options);
        made->setListener([this] { ++changes; });
        return made;
    }

    // Image requests for chapter `sourceOrder`'s pages, by page index.
    std::multiset<int> imageRequests(int sourceOrder) const {
        std::multiset<int> pages;
        const std::string prefix = "http://s/api/v1/manga/1/chapter/" + std::to_string(sourceOrder) + "/page/";
        for (const Request& r : http.requests()) {
            if (r.url.rfind(prefix, 0) == 0) {
                pages.insert(std::stoi(r.url.substr(prefix.size())));
            }
        }
        return pages;
    }

    // After a test that held requests: everything still waiting completes, and its answers run.
    void drain() {
        http.hold(false);
        http.releaseAll();
        main.settle(50);
    }

    int asked(const std::string& operation) const {
        int n = 0;
        for (const Request& r : http.requests()) {
            n += operationOf(r) == operation ? 1 : 0;
        }
        return n;
    }

    // Runs the loader's answers until the page on screen is ready (or failed) and nothing else is loading.
    bool settle(const ReaderModel& model) {
        const bool done = main.runUntil([&] {
            if (model.onPage() && (model.current().state == PageState::Loading || model.current().state == PageState::Waiting)) {
                return false;
            }
            for (int i = 0; i < model.pageCount(); ++i) {
                if (model.page(i).state == PageState::Loading) {
                    return false;
                }
            }
            return true;
        });
        main.settle(50);  // the next chapter's first pages
        return done;
    }
};

std::set<int> pagesIn(const ReaderModel& model, PageState state) {
    std::set<int> pages;
    for (int i = 0; i < model.pageCount(); ++i) {
        if (model.page(i).state == state) {
            pages.insert(i);
        }
    }
    return pages;
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("reader: opens a chapter at the page asked for and loads the window around it") {
    Harness h;
    auto model = h.model();
    model->open(101, 2);
    REQUIRE(model->state() == State::Ready);  // the fake client answers at once
    CHECK(model->chapter().id == 101);
    CHECK(model->pageCount() == 10);
    CHECK(model->position() == 2);
    REQUIRE(h.settle(*model));
    CHECK(model->current().state == PageState::Ready);
    REQUIRE(model->current().image);
    CHECK(model->current().image->size() == core::ImageSize{500, 750});  // fit screen: JPEG at 1/2, within the slack of 480 x 720
    CHECK(pagesIn(*model, PageState::Ready) == std::set<int>{1, 2, 3, 4, 5});  // -1 / +3
    CHECK(h.imageRequests(1) == std::multiset<int>{1, 2, 3, 4, 5});
    CHECK(h.asked("UpdateChapterProgress") == 0);  // nothing changed yet
}

TEST_CASE("reader: turning lets go of pages behind the window and fetches the new ones once") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    REQUIRE(h.settle(*model));
    CHECK(pagesIn(*model, PageState::Ready) == std::set<int>{0, 1, 2, 3});
    for (int i = 0; i < 4; ++i) {
        CHECK(model->step(1));
    }
    CHECK(model->position() == 4);
    REQUIRE(h.settle(*model));
    CHECK(pagesIn(*model, PageState::Ready) == std::set<int>{3, 4, 5, 6, 7});
    CHECK(pagesIn(*model, PageState::Waiting) == std::set<int>{0, 1, 2, 8, 9});
    CHECK(h.imageRequests(1) == std::multiset<int>{0, 1, 2, 3, 4, 5, 6, 7});  // each page downloaded once
    // Back to page 1: it comes from the memory cache (no new download).
    for (int i = 0; i < 3; ++i) {
        CHECK(model->step(-1));
    }
    REQUIRE(h.settle(*model));
    CHECK(model->current().state == PageState::Ready);
    CHECK(h.imageRequests(1).count(1) == 1);
}

TEST_CASE("reader: sides turn by the reading direction; right to left is mirrored") {
    Harness h;
    auto ltr = h.model(ReadingDirection::LeftToRight);
    ltr->open(101, 3);
    CHECK(ltr->turn(Side::Right));
    CHECK(ltr->position() == 4);
    CHECK(ltr->turn(Side::Left));
    CHECK(ltr->position() == 3);

    auto rtl = h.model(ReadingDirection::RightToLeft);
    rtl->open(101, 3);
    CHECK(rtl->turn(Side::Left));
    CHECK(rtl->position() == 4);
    rtl->setDirection(ReadingDirection::LeftToRight);
    CHECK(rtl->turn(Side::Left));
    CHECK(rtl->position() == 3);
    CHECK(rtl->settings().direction == ReadingDirection::LeftToRight);
    CHECK(rtl->settings().mangaId == kMangaId);
    h.main.settle(50);
}

TEST_CASE("reader: progress goes out at most every 5 s, and at once when the last page marks the chapter read") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    model->step(1);
    model->step(1);
    model->onFrame();
    CHECK(h.server.progress.empty());  // not 5 s since opening
    h.now += 5000;
    model->onFrame();
    REQUIRE(h.server.progress.size() == 1);
    CHECK(h.server.progress[0] == nlohmann::json{{"id", 101}, {"lastPageRead", 2}});
    model->step(1);
    h.now += 1000;
    model->onFrame();
    CHECK(h.server.progress.size() == 1);
    while (model->position() < 9) {
        model->step(1);
    }
    CHECK(model->chapterRead());
    model->onFrame();  // the last page: no waiting
    REQUIRE(h.server.progress.size() == 2);
    CHECK(h.server.progress[1] == nlohmann::json{{"id", 101}, {"lastPageRead", 9}, {"isRead", true}});
    CHECK(h.server.find(101)->isRead);
    REQUIRE(h.saved.size() == 2);
    CHECK(h.saved[1].second);
    h.main.settle(50);
}

TEST_CASE("reader: end screens, then the next chapter opens from its prefetched list and pages") {
    Harness h;
    auto model = h.model();
    model->open(101, 6);
    REQUIRE(h.settle(*model));
    CHECK_FALSE(model->nextChapterReady());
    CHECK(model->step(1));  // page 7: the window reaches past the end
    REQUIRE(h.settle(*model));
    CHECK(model->nextChapterReady());
    CHECK(h.asked("FetchChapterPages") == 2);
    CHECK(h.imageRequests(2) == std::multiset<int>{0, 1});  // the next chapter's first two pages
    model->step(1);
    model->step(1);
    CHECK(model->step(1));  // the "next chapter" screen
    CHECK(model->position() == 10);
    CHECK_FALSE(model->onPage());
    REQUIRE(model->nextChapter() != nullptr);
    CHECK(model->nextChapter()->id == 102);

    CHECK(model->step(1));  // chapter 2
    CHECK(model->state() == State::Ready);
    CHECK(model->chapter().id == 102);
    CHECK(model->position() == 0);
    CHECK(model->current().state == PageState::Ready);  // prefetched: on screen at once
    CHECK(h.asked("FetchChapterPages") == 2);  // no new request for its list
    // Leaving chapter 1 saved it as read (its last page was shown).
    CHECK(h.server.find(101)->isRead);
    CHECK(h.server.find(101)->lastPageRead == 9);
    REQUIRE(h.settle(*model));
    CHECK(h.imageRequests(2) == std::multiset<int>{0, 1, 2, 3});

    // Back: the "previous chapter" screen, then chapter 1 at its last page.
    CHECK(model->step(-1));
    CHECK(model->position() == -1);
    REQUIRE(model->previousChapter() != nullptr);
    CHECK(model->previousChapter()->id == 101);
    CHECK(model->previousChapter()->isRead);  // the reader's copy knows what it saved
    CHECK(model->step(-1));
    CHECK(model->chapter().id == 101);
    CHECK(model->position() == 9);
    CHECK(h.asked("FetchChapterPages") == 4);  // chapter 1, and chapter 2's again (page 9 is near the end)
    h.main.settle(50);
}

TEST_CASE("reader: the next chapter opens where it was left, or at the start when it is read") {
    Harness h;
    h.server.find(102)->lastPageRead = 5;
    auto model = h.model();
    model->open(101, 9);
    model->step(1);
    model->step(1);
    CHECK(model->chapter().id == 102);
    CHECK(model->position() == 5);

    Harness read;
    read.server.find(102)->isRead = true;
    read.server.find(102)->lastPageRead = 9;
    auto other = read.model(ReadingDirection::LeftToRight, 1, 0);  // no prefetch: the list comes when it opens
    other->open(101, 9);
    other->step(1);
    other->step(1);
    CHECK(other->chapter().id == 102);
    CHECK(other->position() == 0);
    h.main.settle(50);
    read.main.settle(50);
}

TEST_CASE("reader: nowhere to go before the first or after the last chapter") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    CHECK(model->step(-1));
    CHECK(model->position() == -1);  // "no previous chapter" screen
    CHECK(model->previousChapter() == nullptr);
    CHECK_FALSE(model->step(-1));
    CHECK(model->position() == -1);

    auto last = h.model();
    last->open(103, 3);
    CHECK(last->step(1));
    CHECK(last->position() == 4);
    CHECK(last->nextChapter() == nullptr);
    CHECK_FALSE(last->step(1));
    h.main.settle(50);
}

TEST_CASE("reader: broken pages fail on their own and don't stop page turns; retry loads again") {
    Harness h;
    h.server.brokenPages = {"1/1"};
    h.server.emptyPaths = {3};
    auto model = h.model();
    model->open(101, 0);
    REQUIRE(h.settle(*model));
    CHECK(model->page(1).state == PageState::Failed);
    CHECK(model->page(1).error.kind == image::ImageErrorKind::Corrupt);
    CHECK(model->page(3).state == PageState::Failed);  // no address: fails without a request
    CHECK(h.imageRequests(1).count(3) == 0);
    CHECK(model->page(2).state == PageState::Ready);
    CHECK(model->step(1));
    CHECK(model->step(1));
    CHECK(model->position() == 2);
    CHECK(model->step(-1));
    h.server.brokenPages.clear();
    model->retryPage();
    CHECK(model->current().state == PageState::Loading);
    REQUIRE(h.settle(*model));
    CHECK(model->current().state == PageState::Ready);
    CHECK(h.imageRequests(1).count(1) == 2);
}

TEST_CASE("reader: pages that failed while the server was away load again when turned to, and once a page arrives") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    REQUIRE(h.settle(*model));
    CHECK(pagesIn(*model, PageState::Ready) == std::set<int>{0, 1, 2, 3});

    // The server stops: the prefetched pages still show, the new ones fail as "network".
    h.server.offline = true;
    for (int i = 0; i < 4; ++i) {
        CHECK(model->step(1));
        REQUIRE(h.settle(*model));
    }
    CHECK(model->position() == 4);
    CHECK(model->current().state == PageState::Failed);
    CHECK(model->current().error.kind == image::ImageErrorKind::Network);
    CHECK(pagesIn(*model, PageState::Failed) == std::set<int>{4, 5, 6, 7});
    CHECK(h.imageRequests(1).count(4) == 2);  // as a prefetch, then again when it was turned to
    CHECK(h.imageRequests(1).count(5) == 1);  // not again while no page arrives

    // The server is back: turning to page 5 loads it (no Retry needed), and its arrival reloads the other failed pages.
    h.server.offline = false;
    CHECK(model->step(1));
    REQUIRE(h.settle(*model));
    CHECK(model->current().state == PageState::Ready);
    CHECK(pagesIn(*model, PageState::Ready) == std::set<int>{4, 5, 6, 7, 8});
    CHECK(h.imageRequests(1).count(5) == 2);
    CHECK(h.imageRequests(1).count(6) == 2);
}

TEST_CASE("reader: a page list that doesn't come shows Failed; retry tries again") {
    Harness h;
    h.server.failPages = true;
    auto model = h.model();
    model->open(102, 0);
    CHECK(model->state() == State::Failed);
    CHECK(model->error().kind == api::ApiErrorKind::Server);
    CHECK_FALSE(model->step(1));
    h.server.failPages = false;
    model->retry();
    CHECK(model->state() == State::Ready);
    CHECK(model->chapter().id == 102);
    h.main.settle(50);
}

TEST_CASE("reader: a new fit decodes the pages again for its box") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    REQUIRE(h.settle(*model));
    CHECK(model->current().image->size() == core::ImageSize{500, 750});
    model->setFit(core::PageFit::Width);
    CHECK(model->pageBox() == core::ImageSize{1280, 0});
    REQUIRE(h.settle(*model));
    REQUIRE(model->current().image);
    CHECK(model->current().image->size() == core::ImageSize{1000, 1500});  // never bigger than the image
    CHECK(model->settings().fit == core::PageFit::Width);
}

TEST_CASE("reader: close sends what isn't saved and calls back when the server answered") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    model->step(1);
    h.http.hold(true);
    bool done = false;
    model->close([&] { done = true; });
    CHECK_FALSE(done);  // the update is on its way
    model.reset();      // the reader is gone before the answer
    CHECK(h.http.releaseAll() >= 1);
    CHECK(done);
    REQUIRE(h.server.progress.size() == 1);
    CHECK(h.server.progress[0]["lastPageRead"] == 1);

    // Nothing to save: at once.
    auto again = h.model();
    again->open(101, 1);
    bool now = false;
    again->close([&] { now = true; });
    CHECK(now);
    h.drain();
}

TEST_CASE("reader: pages turned while an update is on its way go out after it when closing") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    model->step(1);
    h.now += 5000;
    h.http.hold(true);
    model->onFrame();  // page 1 on its way
    model->step(1);
    model->step(1);
    bool done = false;
    model->close([&] { done = true; });
    CHECK_FALSE(done);
    while (h.http.releaseNext()) {
    }
    CHECK(done);
    REQUIRE(h.server.progress.size() == 2);
    CHECK(h.server.progress[1]["lastPageRead"] == 3);
    h.drain();
}

TEST_CASE("reader: a failed update is tried again later; the destructor still sends what's left") {
    Harness h;
    h.server.failProgress = true;
    auto model = h.model();
    model->open(101, 0);
    model->step(1);
    h.now += 5000;
    model->onFrame();
    REQUIRE(h.saved.size() == 1);
    CHECK_FALSE(h.saved[0].second);
    model->onFrame();
    CHECK(h.saved.size() == 1);  // not before the interval is over
    h.server.failProgress = false;
    h.now += 5000;
    model->onFrame();
    REQUIRE(h.saved.size() == 2);
    CHECK(h.saved[1].second);
    model->step(1);
    model.reset();
    REQUIRE(h.server.progress.size() == 2);
    CHECK(h.server.progress[1]["lastPageRead"] == 2);
    h.main.settle(50);
}

TEST_CASE("reader: leaving the window cancels its loads; a model destroyed while loading is safe") {
    Harness h;
    h.http.hold(true);
    auto model = h.model(ReadingDirection::LeftToRight, 0, 1);
    model->open(101, 0);  // the page list is held too
    CHECK(model->state() == State::Loading);
    CHECK(h.http.releaseNext());
    CHECK(model->state() == State::Ready);
    CHECK(pagesIn(*model, PageState::Loading) == std::set<int>{0, 1});
    for (int i = 0; i < 4; ++i) {
        model->step(1);
    }
    CHECK(pagesIn(*model, PageState::Loading) == std::set<int>{4, 5});
    CHECK(pagesIn(*model, PageState::Waiting).count(0) == 1);
    h.http.releaseAll();
    REQUIRE(h.settle(*model));
    // Pages 0 and 1 were cancelled before they were downloaded.
    CHECK(h.imageRequests(1).count(0) == 1);
    CHECK(pagesIn(*model, PageState::Ready) == std::set<int>{4, 5});
    model->step(1);
    model.reset();  // page 6 on its way
    h.drain();
}
}
