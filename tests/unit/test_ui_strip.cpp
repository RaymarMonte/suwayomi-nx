// WP-13b: the webtoon strip's model (src/ui/reader/StripModel) on a FakeHttpClient with a real ImageLoader: opening at the
// spot note or a page's top, the window of pages near the screen (and letting go of the far ones), the neighbouring chapters
// joined above and below with the view still (a fourth one cuts the farthest out), the chapter being read following the
// middle line (the old one saved at once, read once its end showed), the progress with the spot in one request, failed pages
// and their retry, the strip's width; and (WP-13d) the zoom: the sharper decodes of the pages on screen, let go when they
// leave it, none where the page's own decode has all the detail, back to zoom 1 on a new width or screen.
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
#include "core/StripMath.hpp"
#include "image/ImageLoader.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/reader/StripModel.hpp"

using namespace snx;
using net::FakeHttpClient;
using net::Request;
using ui::StripModel;
using PageState = ui::ReaderPage::State;
using State = ui::StripModel::State;

namespace {

constexpr int kMangaId = 1;

struct FakeChapter {
    int id = 0;
    int sourceOrder = 0;
    int pages = 10;
    bool isRead = false;
    int lastPageRead = 0;
    std::string note;  // the snx_strip_spot meta note ("" = none)
};

// The server: four chapters (ids 101..104 with 10, 2, 2 and 2 pages). Pages are Alpha's 1000 x 1500 JPEG (960 units tall
// in a 640-wide strip), or Gamma's 800 x 12000 WebP strip for the pages in `tallPages` (9600 units).
struct FakeServer {
    std::vector<FakeChapter> chapters{{101, 1, 10, false, 0, {}}, {102, 2, 2, false, 0, {}}, {103, 3, 2, false, 0, {}}, {104, 4, 2, false, 0, {}}};
    std::set<std::string> brokenPages;  // "<sourceOrder>/<page>": a corrupt JPEG
    std::set<std::string> tallPages;    // "<sourceOrder>/<page>": Gamma's tall strip
    std::set<int> failLists;            // chapter ids whose page list fails
    std::vector<nlohmann::json> progress;  // every progress patch, with "id" (and "spot" for UpdateStripProgress)
    std::string image = readFixture("images/alpha-p1.jpg");
    std::string tall = readFixture("images/gamma-strip.webp");
    std::string corrupt = readFixture("images/broken-corrupt.jpg");

    FakeChapter* find(int id) {
        for (FakeChapter& c : chapters) {
            if (c.id == id) {
                return &c;
            }
        }
        return nullptr;
    }

    nlohmann::json chapterJson(const FakeChapter& c) const {
        nlohmann::json meta = nlohmann::json::array();
        if (!c.note.empty()) {
            meta.push_back({{"key", "snx_strip_spot"}, {"value", c.note}});
        }
        return {{"id", c.id}, {"mangaId", kMangaId}, {"sourceOrder", c.sourceOrder}, {"isRead", c.isRead},
                {"lastPageRead", c.lastPageRead}, {"pageCount", c.pages}, {"meta", meta}};
    }

    net::HttpResult answer(const Request& request) {
        const std::string prefix = "http://s/api/v1/manga/1/chapter/";
        if (request.url.rfind(prefix, 0) == 0) {
            const std::string rest = request.url.substr(prefix.size());  // "<sourceOrder>/page/<n>"
            const std::string order = rest.substr(0, rest.find('/'));
            const std::string key = order + "/" + rest.substr(rest.rfind('/') + 1);
            if (brokenPages.count(key)) {
                return FakeHttpClient::answer(200, corrupt);
            }
            return FakeHttpClient::answer(200, tallPages.count(key) ? tall : image);
        }
        const std::string operation = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (operation == "FetchChapterPages") {
            FakeChapter* c = find(vars["chapterId"].get<int>());
            if (c == nullptr || failLists.count(c->id)) {
                return FakeHttpClient::answer(500, "oops");
            }
            nlohmann::json pages = nlohmann::json::array();
            for (int i = 0; i < c->pages; ++i) {
                pages.push_back("/api/v1/manga/1/chapter/" + std::to_string(c->sourceOrder) + "/page/" + std::to_string(i));
            }
            return FakeHttpClient::answer(200, nlohmann::json{{"data", {{"fetchChapterPages", {
                {"pages", pages}, {"chapter", chapterJson(*c)}, {"syncConflict", nullptr}}}}}}.dump());
        }
        if (operation == "UpdateChapterProgress" || operation == "UpdateStripProgress") {
            FakeChapter* c = find(vars["id"].get<int>());
            REQUIRE(c != nullptr);
            nlohmann::json patch = vars["patch"];
            patch["id"] = c->id;
            c->lastPageRead = patch["lastPageRead"].get<int>();
            if (patch.contains("isRead") && patch["isRead"].get<bool>()) {
                c->isRead = true;
            }
            nlohmann::json data = {{"updateChapter", {{"chapter", {{"id", c->id}, {"lastPageRead", c->lastPageRead},
                                                                    {"isRead", c->isRead}, {"pageCount", c->pages}}}}}};
            if (operation == "UpdateStripProgress") {
                c->note = vars["spot"].get<std::string>();
                patch["spot"] = c->note;
                data["setChapterMeta"] = {{"meta", {{"key", "snx_strip_spot"}, {"value", c->note}}}};
            }
            progress.push_back(patch);
            return FakeHttpClient::answer(200, nlohmann::json{{"data", data}}.dump());
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

    // A landscape strip at half the width: 640 units wide on a 1280 x 720 screen; bands 240 tall.
    std::unique_ptr<StripModel> model(double behind = 1, double ahead = 3, int zoomPages = 2) {
        core::Manga manga;
        manga.id = kMangaId;
        manga.title = "Gamma";
        ui::StripOptions options;
        options.screensBehind = behind;
        options.screensAhead = ahead;
        options.zoomPages = zoomPages;
        options.clock = [this] { return now; };
        options.onSaved = [this](const core::ReadProgress& p, bool ok) { saved.emplace_back(p, ok); };
        auto made = std::make_unique<StripModel>(*client, *images, manga, chapters(), options);
        made->setListener([this] { ++changes; });
        return made;
    }

    int asked(const std::string& operation) const {
        int n = 0;
        for (const Request& r : http.requests()) {
            n += operationOf(r) == operation ? 1 : 0;
        }
        return n;
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

    // Runs the loader's answers until no page in the strip is loading.
    bool settle(const StripModel& model) {
        const bool done = main.runUntil([&] {
            for (int item = 0; item < model.layout().size(); ++item) {
                if (model.pageAt(item).state == PageState::Loading) {
                    return false;
                }
            }
            return true;
        });
        main.settle(50);
        return done;
    }
};

// The pages of chapter `chapterId` in state `state`.
std::set<int> pagesIn(const StripModel& model, int chapterId, PageState state) {
    std::set<int> pages;
    const core::StripLayout& layout = model.layout();
    for (int item = 0; item < layout.size(); ++item) {
        const core::StripItem& it = layout.item(item);
        if (it.kind == core::StripItemKind::Page && it.chapterId == chapterId && model.pageAt(item).state == state) {
            pages.insert(it.page);
        }
    }
    return pages;
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("strip: opens at the page's top and loads the pages near the screen") {
    Harness h;
    auto model = h.model();
    model->open(101, 2);
    REQUIRE(model->state() == State::Ready);
    CHECK(model->chapter().id == 101);
    CHECK(model->pageCount() == 10);
    CHECK(model->stripWidth() == doctest::Approx(640));
    const core::StripLayout& layout = model->layout();
    CHECK(layout.size() == 12);  // a band, ten pages, a band
    CHECK(layout.item(0).kind == core::StripItemKind::Band);
    CHECK(layout.item(0).height == doctest::Approx(240));
    const int p2 = layout.find(101, 2);
    CHECK(layout.top() == doctest::Approx(layout.itemTop(p2)));  // the page's top at the screen's top
    REQUIRE(h.settle(*model));
    CHECK(layout.item(p2).height == doctest::Approx(960));  // 1000 x 1500 at 640 wide
    CHECK(layout.top() == doctest::Approx(240 + 2 * 960));
    CHECK(model->page() == 2);
    CHECK(model->spot() == doctest::Approx(360.0 / 960));
    // One screen behind, three ahead: [1440, 5040) touches pages 1..4.
    CHECK(pagesIn(*model, 101, PageState::Ready) == std::set<int>{1, 2, 3, 4});
    CHECK(h.imageRequests(1) == std::multiset<int>{1, 2, 3, 4});
    CHECK(model->decodeBox() == core::ImageSize{640, 0});
    CHECK(model->chaptersHeld() == std::vector<int>{101});
}

TEST_CASE("strip: opens at the spot note when it is for the page asked, else at the page's top") {
    SUBCASE("the note's page") {
        Harness h;
        h.server.chapters[0].lastPageRead = 3;
        h.server.chapters[0].note = "3:0.5000";
        auto model = h.model();
        model->open(101, 3);
        REQUIRE(h.settle(*model));
        const core::StripPlace middle = model->layout().middle();
        CHECK(middle.item == model->layout().find(101, 3));
        CHECK(middle.share == doctest::Approx(0.5));
        CHECK(model->page() == 3);
        CHECK(model->spot() == doctest::Approx(0.5));
        h.now += 10000;
        model->onFrame();
        CHECK(h.asked("UpdateStripProgress") == 0);  // the server has that spot already
    }
    SUBCASE("another page (another app moved on)") {
        Harness h;
        h.server.chapters[0].lastPageRead = 3;
        h.server.chapters[0].note = "5:0.5000";
        auto model = h.model();
        model->open(101, 3);
        REQUIRE(h.settle(*model));
        CHECK(model->layout().top() == doctest::Approx(model->layout().itemTop(model->layout().find(101, 3))));
    }
    SUBCASE("switching from the page reader: the page's top") {
        Harness h;
        h.server.chapters[0].lastPageRead = 3;
        h.server.chapters[0].note = "3:0.5000";
        auto model = h.model();
        model->open(101, 3, StripModel::Open::PageTop);
        REQUIRE(h.settle(*model));
        CHECK(model->layout().top() == doctest::Approx(model->layout().itemTop(model->layout().find(101, 3))));
    }
}

TEST_CASE("strip: a page arriving above the screen keeps the view still") {
    Harness h;
    h.server.tallPages = {"1/1", "1/2"};
    auto model = h.model(1, 1);
    model->open(101, 3);
    const core::StripLayout& layout = model->layout();
    const int p3 = layout.find(101, 3);
    CHECK(layout.item(p3 - 1).height == doctest::Approx(960));  // a guess: 1.5 x the width
    REQUIRE(h.settle(*model));
    // Page 2 (behind) arrived 9600 tall; page 3 is still at the screen's top (pinned there by the open).
    CHECK(layout.item(p3 - 1).height == doctest::Approx(9600));
    CHECK(layout.top() == doctest::Approx(layout.itemTop(p3)));
    // After a scroll the middle line holds instead: up into page 2, so page 1 comes into the window and arrives.
    model->scrollTo(layout.itemTop(p3) - 9000);
    const core::StripPlace before = layout.middle();
    CHECK(before.item == p3 - 1);
    const double topBefore = layout.top();
    const double p2Before = layout.itemTop(p3 - 1);
    REQUIRE(h.settle(*model));
    CHECK(layout.item(p3 - 2).height == doctest::Approx(9600));
    const core::StripPlace after = layout.middle();
    CHECK(after.item == before.item);
    CHECK(after.share == doctest::Approx(before.share));
    CHECK(layout.top() - topBefore == doctest::Approx(layout.itemTop(p3 - 1) - p2Before));
}

TEST_CASE("strip: the neighbouring chapters join below and above with the view still") {
    Harness h;
    auto model = h.model();
    model->open(102, 0);  // two pages: both ends are near
    REQUIRE(h.settle(*model));
    CHECK(model->chaptersHeld() == std::vector<int>{101, 102, 103});
    const core::StripLayout& layout = model->layout();
    const int first = layout.find(102, 0);
    CHECK(first == 1 + 10 + 1);  // band, chapter 1's ten pages, band
    CHECK(layout.top() == doctest::Approx(layout.itemTop(first)));  // still at chapter 2's start
    CHECK(model->chapter().id == 102);
    // The bands know their chapters.
    const ui::StripBand between = model->band(first - 1);
    CHECK(between.above == 101);
    CHECK(between.below == 102);
    const ui::StripBand top = model->band(0);
    CHECK(top.above == 0);  // no previous chapter
    CHECK(top.below == 101);
    const ui::StripBand end = model->band(layout.size() - 1);
    CHECK(end.above == 103);
    CHECK(end.below == 104);  // not joined yet: chapter 3 isn't the one being read
    CHECK(h.asked("FetchChapterPages") == 3);
}

TEST_CASE("strip: the middle line moving into the next chapter saves the old one at once, read") {
    Harness h;
    auto model = h.model();
    model->open(102, 1);
    REQUIRE(h.settle(*model));
    const core::StripLayout& layout = model->layout();
    // Down until the middle line is in chapter 3's first page.
    const int next = layout.find(103, 0);
    model->scrollTo(layout.itemTop(next) + 100 - layout.viewHeight() / 2);
    REQUIRE(h.settle(*model));
    CHECK(model->chapter().id == 103);
    CHECK(model->page() == 0);
    REQUIRE(!h.server.progress.empty());
    const nlohmann::json& left = h.server.progress.back();
    CHECK(left["id"] == 102);
    CHECK(left["lastPageRead"] == 1);
    CHECK(left["isRead"] == true);  // its last page's bottom came on screen
    CHECK(left["spot"].get<std::string>().rfind("1:", 0) == 0);
    CHECK(h.server.find(102)->isRead);
}

TEST_CASE("strip: a fourth chapter cuts the farthest out") {
    Harness h;
    auto model = h.model();
    model->open(102, 0);
    REQUIRE(h.settle(*model));
    REQUIRE(model->chaptersHeld() == std::vector<int>{101, 102, 103});
    const core::StripLayout& layout = model->layout();
    model->scrollTo(layout.itemTop(layout.find(103, 1)));  // reading chapter 3 now: chapter 4 joins, chapter 1 goes
    REQUIRE(h.settle(*model));
    CHECK(model->chapter().id == 103);
    CHECK(model->chaptersHeld() == std::vector<int>{102, 103, 104});
    CHECK(layout.find(101, 0) == -1);
    CHECK(layout.item(0).kind == core::StripItemKind::Band);
    CHECK(model->band(0).below == 102);
    // The view didn't move: chapter 3's page 1 is still at the top.
    CHECK(layout.top() == doctest::Approx(layout.itemTop(layout.find(103, 1))));
}

TEST_CASE("strip: progress with the spot, at most every progressSyncMs, in one request") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    REQUIRE(h.settle(*model));
    model->scrollBy(480);  // the middle line 840 units into page 0: 0.875
    model->onFrame();
    CHECK(h.asked("UpdateStripProgress") == 0);  // within the interval
    h.now += 5000;
    model->onFrame();
    REQUIRE(h.asked("UpdateStripProgress") == 1);
    CHECK(h.asked("UpdateChapterProgress") == 0);
    CHECK(h.server.progress.back()["lastPageRead"] == 0);
    CHECK(h.server.progress.back()["spot"] == "0:0.8750");
    REQUIRE(!h.saved.empty());
    CHECK(h.saved.back().second);
    REQUIRE(h.saved.back().first.spot.has_value());
    CHECK(*h.saved.back().first.spot == doctest::Approx(0.875));
    h.now += 5000;
    model->onFrame();
    CHECK(h.asked("UpdateStripProgress") == 1);  // nothing moved
}

TEST_CASE("strip: closing saves what is left, then calls done") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    REQUIRE(h.settle(*model));
    model->scrollBy(1500);
    bool done = false;
    model->close([&] { done = true; });
    CHECK(done);
    REQUIRE(h.asked("UpdateStripProgress") == 1);
    CHECK(h.server.progress.back()["lastPageRead"] == 1);
}

TEST_CASE("strip: pages that leave the window are let go and load again when they come back") {
    Harness h;
    auto model = h.model();
    model->open(101, 0);
    REQUIRE(h.settle(*model));
    CHECK(pagesIn(*model, 101, PageState::Ready).count(0) == 1);
    const core::StripLayout& layout = model->layout();
    model->scrollTo(layout.itemTop(layout.find(101, 7)));
    REQUIRE(h.settle(*model));
    CHECK(pagesIn(*model, 101, PageState::Ready) == std::set<int>{6, 7, 8, 9});
    CHECK(pagesIn(*model, 101, PageState::Waiting).count(0) == 1);
    CHECK(model->pagesHeld() >= 4);
}

TEST_CASE("strip: a failed page is a one-screen band, and retries") {
    Harness h;
    h.server.brokenPages = {"1/2"};
    auto model = h.model();
    model->open(101, 2);
    REQUIRE(h.settle(*model));
    const core::StripLayout& layout = model->layout();
    const int p2 = layout.find(101, 2);
    CHECK(model->pageAt(p2).state == PageState::Failed);
    CHECK(layout.item(p2).height == doctest::Approx(720));
    CHECK(layout.top() == doctest::Approx(layout.itemTop(p2)));
    h.server.brokenPages.clear();
    CHECK(model->retryNearest());
    REQUIRE(h.settle(*model));
    CHECK(model->pageAt(p2).state == PageState::Ready);
    CHECK(layout.item(p2).height == doctest::Approx(960));
    CHECK_FALSE(model->retryNearest());  // nothing failed on screen now
}

TEST_CASE("strip: a neighbouring chapter's list that failed shows on its band and retries") {
    Harness h;
    h.server.failLists = {102};
    auto model = h.model();
    model->open(101, 9);
    REQUIRE(h.settle(*model));
    const core::StripLayout& layout = model->layout();
    const int end = layout.size() - 1;
    CHECK(model->chaptersHeld() == std::vector<int>{101});
    CHECK(model->band(end).edge == ui::StripBand::Edge::Failed);
    CHECK(model->band(end).below == 102);
    h.server.failLists.clear();
    CHECK(model->retryItem(end));
    REQUIRE(h.settle(*model));
    CHECK(model->chaptersHeld() == std::vector<int>{101, 102});
}

TEST_CASE("strip: the width follows the setting and the orientation, the middle line's place stays") {
    Harness h;
    auto model = h.model();
    model->open(101, 2);
    REQUIRE(h.settle(*model));
    model->scrollBy(100);
    const core::StripPlace before = model->layout().middle();
    model->setStripWidth(core::StripWidth::Full);
    CHECK(model->stripWidth() == doctest::Approx(1280));
    CHECK(model->decodeBox() == core::ImageSize{1280, 0});
    const core::StripPlace after = model->layout().middle();
    CHECK(after.item == before.item);
    CHECK(after.share == doctest::Approx(before.share));
    CHECK(model->layout().item(after.item).height == doctest::Approx(1920));
    // Portrait: the whole width (720), whatever the setting.
    model->setScreen({720, 1280}, {720, 1280}, core::ReaderOrientation::PortraitLeft);
    CHECK(model->stripWidth() == doctest::Approx(720));
    CHECK(model->layout().viewHeight() == doctest::Approx(1280));
    CHECK(model->layout().middle().item == before.item);
    REQUIRE(h.settle(*model));
    CHECK(model->pageAt(before.item).state == PageState::Ready);
    CHECK(model->pageAt(before.item).image->size().width <= 720 * 115 / 100);
}

TEST_CASE("strip: zoomed, the pages on screen decode sharper (two at most), let go when they leave it or at zoom 1") {
    Harness h;
    auto model = h.model();
    model->open(101, 2);
    REQUIRE(h.settle(*model));
    const core::StripLayout& layout = model->layout();
    const int p2 = layout.find(101, 2);
    const int p3 = layout.find(101, 3);
    REQUIRE(model->shownImage(p2));
    CHECK(model->shownImage(p2)->size() == core::ImageSize{640, 960});  // Alpha's 1000 x 1500 at the strip's 640
    // 2 x: the screen shows 360 units. The seam of pages 2 and 3 at the middle line: both on screen.
    model->setView(2, layout.itemTop(p3) - 180);
    CHECK(model->zoom() == doctest::Approx(2));
    CHECK(layout.viewHeight() == doctest::Approx(360));
    CHECK(layout.top() == doctest::Approx(layout.itemTop(p3) - 180));
    CHECK(model->zoomBox() == core::ImageSize{1280, 0});
    CHECK(model->sharpHeld() == 0);  // nothing decodes again before the zoom has settled
    CHECK_FALSE(model->sharpLoading());
    model->sharpenOnScreen();
    CHECK(model->sharpLoading());
    REQUIRE(h.main.runUntil([&] { return !model->sharpLoading(); }));
    CHECK(model->sharpHeld() == 2);
    CHECK(model->shownImage(p2)->size() == core::ImageSize{1000, 1500});  // all the source has (the box asks for 1280)
    CHECK(model->shownImage(p3)->size() == core::ImageSize{1000, 1500});
    CHECK(model->pageAt(p2).image->size() == core::ImageSize{640, 960});  // the page's own decode stays as it is
    CHECK(model->layout().item(p2).height == doctest::Approx(960));        // and the strip's layout too
    // Again: nothing changes.
    const int changes = h.changes;
    model->sharpenOnScreen();
    CHECK_FALSE(model->sharpLoading());
    CHECK(h.changes == changes);
    // Page 2 leaves the screen: its sharper decode goes at once, page 3 keeps its own.
    model->setView(2, layout.itemTop(p3) + 10);
    CHECK(model->sharpHeld() == 1);
    CHECK(model->shownImage(p2)->size() == core::ImageSize{640, 960});
    CHECK(model->shownImage(p3)->size() == core::ImageSize{1000, 1500});
    // Zoomed out a step (1.5 x, box 960): the sharper decode still has the detail wanted, so it stays (no decode again).
    model->setView(1.5f, layout.top());
    model->sharpenOnScreen();
    CHECK_FALSE(model->sharpLoading());
    CHECK(model->shownImage(p3)->size() == core::ImageSize{1000, 1500});
    // Back to 1: once settled, every sharper decode goes.
    model->setView(1, layout.top());
    CHECK(layout.viewHeight() == doctest::Approx(720));
    model->sharpenOnScreen();
    CHECK(model->sharpHeld() == 0);
    CHECK(model->shownImage(p3)->size() == core::ImageSize{640, 960});
}

TEST_CASE("strip: zoomed, at most zoomPages pages, none whose own decode has all the detail; a new width goes back to 1") {
    Harness h;
    // zoomPages 1 (safe mode): only the page nearest the middle line.
    auto model = h.model(1, 3, 1);
    model->open(101, 2);
    REQUIRE(h.settle(*model));
    const core::StripLayout& layout = model->layout();
    const int p2 = layout.find(101, 2);
    const int p3 = layout.find(101, 3);
    model->setView(2, layout.itemTop(p3) - 100);  // the middle line 80 into page 3, page 2 still on screen
    model->sharpenOnScreen();
    REQUIRE(h.main.runUntil([&] { return !model->sharpLoading(); }));
    CHECK(model->sharpHeld() == 1);
    CHECK(model->shownImage(p3)->size() == core::ImageSize{1000, 1500});
    CHECK(model->shownImage(p2)->size() == core::ImageSize{640, 960});
    // A new strip width goes back to zoom 1 (the screen shows 720 units again).
    model->setStripWidth(core::StripWidth::Full);
    CHECK(model->zoom() == doctest::Approx(1));
    CHECK(layout.viewHeight() == doctest::Approx(720));
    REQUIRE(h.settle(*model));
    // Full width (1280): Alpha's pages decode whole (1000 x 1500), so a zoom has nothing sharper to decode.
    const int p = layout.middle().item;
    REQUIRE(model->shownImage(p));
    CHECK(model->shownImage(p)->size() == core::ImageSize{1000, 1500});
    model->setView(2, layout.top());
    model->sharpenOnScreen();
    CHECK_FALSE(model->sharpLoading());
    CHECK(model->sharpHeld() == 0);
    // Rotating (a new screen) goes back to 1 as well.
    model->setScreen({720, 1280}, {720, 1280}, core::ReaderOrientation::PortraitLeft);
    CHECK(model->zoom() == doctest::Approx(1));
    CHECK(layout.viewHeight() == doctest::Approx(1280));
}

}  // TEST_SUITE
