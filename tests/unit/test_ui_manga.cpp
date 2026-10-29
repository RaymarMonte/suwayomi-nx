// WP-10: the manga details screen's model (src/ui/manga/MangaModel) on a FakeHttpClient: loading the details and every
// chapter page, sort and filter, "Continue" (continueReading, D43), the chapter actions, reload and refresh from source.
// The fake server below keeps one manga's chapters in memory and answers Manga, Chapters (paged, both orders),
// UpdateChapters and FetchMangaAndChapters like the real one.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "ApiTestKit.hpp"
#include "api/SuwayomiClient.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/manga/MangaModel.hpp"

using namespace snx;
using core::Chapter;
using net::FakeHttpClient;
using net::Request;
using ui::ChapterFilter;
using ui::ChapterSort;
using ui::ContinuePoint;
using ui::MangaModel;
using Action = ui::MangaModel::Action;
using State = ui::MangaModel::State;

namespace {

constexpr int kMangaId = 4;

// Chapter n (1 = oldest) has sourceOrder n and id 1000 + n.
Chapter chapterNo(int n) {
    Chapter c;
    c.id = 1000 + n;
    c.mangaId = kMangaId;
    c.name = "Chapter " + std::to_string(n);
    c.chapterNumber = n;
    c.sourceOrder = n;
    c.uploadDate = 1700000000000LL + n * 86400000LL;
    return c;
}

std::vector<Chapter> chapterRange(int count) {
    std::vector<Chapter> list;
    for (int n = 1; n <= count; ++n) {
        list.push_back(chapterNo(n));
    }
    return list;
}

nlohmann::json chapterJson(const Chapter& c) {
    return {{"id", c.id},           {"mangaId", c.mangaId},
            {"name", c.name},       {"chapterNumber", c.chapterNumber},
            {"sourceOrder", c.sourceOrder}, {"uploadDate", std::to_string(c.uploadDate)},
            {"scanlator", nullptr}, {"isRead", c.isRead},
            {"isBookmarked", c.isBookmarked}, {"isDownloaded", false},
            {"lastPageRead", c.lastPageRead}, {"pageCount", c.pageCount},
            {"lastReadAt", std::to_string(c.lastReadAt)}};
}

nlohmann::json data(nlohmann::json inner) {
    return nlohmann::json{{"data", std::move(inner)}};
}

struct FakeServer {
    std::vector<Chapter> chapters;  // oldest first
    bool failManga = false;
    bool failRefresh = false;
    bool failUpdate = false;
    int failChaptersPage = -1;  // this page (by offset / first) answers HTTP 500
    std::string description = "A long story.";
    long long clock = 5000;  // lastReadAt stamps for marked chapters

    nlohmann::json manga() const {
        const int unread = static_cast<int>(
            std::count_if(chapters.begin(), chapters.end(), [](const Chapter& c) { return !c.isRead; }));
        return {{"id", kMangaId},       {"title", "Delta Big"}, {"thumbnailUrl", "/api/v1/manga/4/thumbnail"},
                {"author", "Someone"},  {"artist", ""},         {"description", description},
                {"genre", {"Action"}},  {"status", "ONGOING"},  {"inLibrary", true},
                {"unreadCount", unread}, {"downloadCount", 0}};
    }

    net::HttpResult answer(const Request& request) {
        const std::string operation = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (operation == "Manga") {
            if (failManga) {
                return FakeHttpClient::answer(500, "oops");
            }
            return FakeHttpClient::answer(200, data({{"manga", manga()}}).dump());
        }
        if (operation == "Chapters") {
            const int first = vars["first"].get<int>();
            const int offset = vars["offset"].get<int>();
            if (failChaptersPage >= 0 && offset / first == failChaptersPage) {
                return FakeHttpClient::answer(500, "oops");
            }
            std::vector<Chapter> ordered = chapters;
            if (vars["order"] == "DESC") {
                std::reverse(ordered.begin(), ordered.end());
            }
            nlohmann::json nodes = nlohmann::json::array();
            for (int i = offset; i < offset + first && i < static_cast<int>(ordered.size()); ++i) {
                nodes.push_back(chapterJson(ordered[static_cast<std::size_t>(i)]));
            }
            const bool more = offset + first < static_cast<int>(ordered.size());
            return FakeHttpClient::answer(
                200, data({{"chapters", {{"nodes", nodes}, {"totalCount", ordered.size()},
                                         {"pageInfo", {{"hasNextPage", more}, {"endCursor", "x"}}}}}})
                         .dump());
        }
        if (operation == "UpdateChapters") {
            if (failUpdate) {
                return FakeHttpClient::answer(500, "oops");
            }
            const nlohmann::json& patch = vars["patch"];
            nlohmann::json changed = nlohmann::json::array();
            for (const int id : vars["ids"].get<std::vector<int>>()) {
                for (Chapter& c : chapters) {
                    if (c.id != id) {
                        continue;
                    }
                    if (patch.contains("isRead")) {
                        c.isRead = patch["isRead"].get<bool>();
                        c.lastReadAt = ++clock;
                    }
                    if (patch.contains("isBookmarked")) {
                        c.isBookmarked = patch["isBookmarked"].get<bool>();
                    }
                    if (patch.contains("lastPageRead")) {
                        c.lastPageRead = patch["lastPageRead"].get<int>();
                    }
                    changed.push_back(chapterJson(c));
                }
            }
            return FakeHttpClient::answer(200, data({{"updateChapters", {{"chapters", changed}}}}).dump());
        }
        if (operation == "FetchMangaAndChapters") {
            if (failRefresh) {
                return FakeHttpClient::answer(500, "oops");
            }
            description = "Refreshed from the source.";
            chapters.push_back(chapterNo(static_cast<int>(chapters.size()) + 1));  // the source has a new chapter
            nlohmann::json ids = nlohmann::json::array();
            for (const Chapter& c : chapters) {
                ids.push_back({{"id", c.id}});
            }
            return FakeHttpClient::answer(
                200, data({{"fetchMangaAndChapters", {{"manga", manga()}, {"chapters", ids}}}}).dump());
        }
        return FakeHttpClient::answer(404);
    }
};

// The model on a fake client whose calls complete inside the call (no deliver), so every step is synchronous.
struct Harness {
    FakeServer server;
    FakeHttpClient http;
    std::unique_ptr<api::SuwayomiClient> client;
    int changes = 0;

    explicit Harness(int chapterCount = 300) {
        server.chapters = chapterRange(chapterCount);
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = "http://192.168.1.10:4567";
        api::ClientOptions options;
        options.chapterPageSize = 100;
        client = std::make_unique<api::SuwayomiClient>(http, settings, options);
    }

    std::unique_ptr<MangaModel> model(std::size_t maxChapters = 10000) {
        core::Manga fromLibrary;
        fromLibrary.id = kMangaId;
        fromLibrary.title = "Delta Big";
        fromLibrary.unreadCount = 300;
        auto made = std::make_unique<MangaModel>(*client, fromLibrary, maxChapters);
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

    nlohmann::json lastVariables(const std::string& operation) const {
        nlohmann::json found;
        for (const Request& r : http.requests()) {
            if (operationOf(r) == operation) {
                found = variablesOf(r);
            }
        }
        return found;
    }
};

std::vector<int> sourceOrders(const std::vector<Chapter>& list) {
    std::vector<int> out;
    for (const Chapter& c : list) {
        out.push_back(c.sourceOrder);
    }
    return out;
}

std::size_t indexOfOrder(const MangaModel& model, int sourceOrder) {
    const auto& items = model.items();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].sourceOrder == sourceOrder) {
            return i;
        }
    }
    FAIL("chapter " << sourceOrder << " is not in the list");
    return 0;
}

// --- continueReading: a table of chapter states -> where Continue starts ---

struct ChapterState {
    bool read = false;
    int page = 0;           // lastPageRead
    long long readAt = 0;   // lastReadAt
};

struct ContinueCase {
    const char* name;
    std::vector<ChapterState> chapters;  // chapter 1 (oldest) first
    ContinuePoint::Kind kind;
    int chapter;  // sourceOrder of the expected chapter (0 when none)
    int page;
};

const ChapterState R{true, 0, 0};    // read, no time stamp
const ChapterState U{false, 0, 0};   // unread, untouched

std::vector<Chapter> fromStates(const std::vector<ChapterState>& states) {
    std::vector<Chapter> list;
    for (std::size_t i = 0; i < states.size(); ++i) {
        Chapter c = chapterNo(static_cast<int>(i) + 1);
        c.isRead = states[i].read;
        c.lastPageRead = states[i].page;
        c.lastReadAt = states[i].readAt;
        list.push_back(c);
    }
    return list;
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("continueReading: the D43 rule, case by case") {
    using K = ContinuePoint::Kind;
    const std::vector<ContinueCase> cases = {
        {"no chapters", {}, K::None, 0, 0},
        {"nothing read: the first chapter", {U, U, U}, K::Start, 1, 0},
        {"one chapter, unread", {U}, K::Start, 1, 0},
        {"every chapter read", {R, R, R}, K::AllRead, 0, 0},
        {"one chapter, read", {R}, K::AllRead, 0, 0},
        {"read in order, no time stamps: the next one", {R, R, U, U}, K::Continue, 3, 0},
        {"a gap, no time stamps: after the last read one", {R, U, R, U}, K::Continue, 4, 0},
        {"a gap and the end read: back to the gap", {R, U, R, R}, K::Continue, 2, 0},
        {"nothing read, progress in chapter 2: resume it", {U, {false, 4, 0}, U}, K::Continue, 2, 4},
        {"nothing read, progress in two: the first one", {U, {false, 3, 0}, {false, 7, 0}}, K::Continue, 2, 3},
        {"read most recently, unfinished: resume it", {{true, 0, 10}, {false, 6, 20}, U}, K::Continue, 2, 6},
        {"a newer stamp on an unread chapter at page 0 (marked unread) doesn't count", {{false, 0, 50}, {true, 0, 10}, U},
         K::Continue, 3, 0},
        {"marked unread after reading: on from the read one", {{true, 0, 10}, {false, 0, 20}, U}, K::Continue, 2, 0},
        {"everything marked unread at once (equal stamps): the start", {{false, 0, 9}, {false, 0, 9}, {false, 0, 9}},
         K::Start, 1, 0},
        {"newest stamp is a finished chapter: the next unread", {{true, 0, 10}, {true, 0, 30}, U, U}, K::Continue, 3, 0},
        {"re-read an old chapter: forward to the next unread, with its progress",
         {{true, 0, 50}, R, R, {false, 5, 10}, U}, K::Continue, 4, 5},
        {"the stale unfinished chapter loses to a newer finished one",
         {{false, 3, 10}, {true, 0, 40}, U}, K::Continue, 3, 0},
        {"time stamps tie: the later chapter counts", {{false, 4, 70}, {true, 0, 70}, U}, K::Continue, 3, 0},
        {"finished last chapter but an older gap: the gap", {{true, 0, 10}, U, {true, 0, 20}}, K::Continue, 2, 0},
        {"every chapter read, with stamps", {{true, 0, 1}, {true, 0, 2}}, K::AllRead, 0, 0},
        {"a read chapter's progress is ignored", {{true, 9, 0}, U}, K::Continue, 2, 0},
        {"a negative page is no progress", {{false, -3, 5}, U}, K::Start, 1, 0},
    };
    for (const ContinueCase& test : cases) {
        CAPTURE(test.name);
        const std::vector<Chapter> chapters = fromStates(test.chapters);
        const ContinuePoint point = ui::continueReading(chapters);
        CHECK(point.kind == test.kind);
        CHECK(point.chapterId == (test.chapter == 0 ? 0 : 1000 + test.chapter));
        CHECK(point.page == test.page);
        // The list's order doesn't matter (the server sends newest first).
        std::vector<Chapter> reversed(chapters.rbegin(), chapters.rend());
        const ContinuePoint again = ui::continueReading(reversed);
        CHECK(again.kind == point.kind);
        CHECK(again.chapterId == point.chapterId);
        CHECK(again.page == point.page);
    }
}

TEST_CASE("continueReading: 300 chapters, the first 149 read; ties in sourceOrder go by id") {
    std::vector<Chapter> chapters = chapterRange(300);
    for (int n = 0; n < 149; ++n) {
        chapters[static_cast<std::size_t>(n)].isRead = true;
    }
    ContinuePoint point = ui::continueReading(chapters);
    CHECK(point.kind == ContinuePoint::Kind::Continue);
    CHECK(point.chapterId == 1150);

    // Two scanlations of chapter 1 (same sourceOrder): the lower id comes first in reading order.
    Chapter a = chapterNo(1);
    Chapter b = chapterNo(1);
    a.id = 7;
    b.id = 3;
    point = ui::continueReading({a, b});
    CHECK(point.kind == ContinuePoint::Kind::Start);
    CHECK(point.chapterId == 3);
}

TEST_CASE("chapter texts: dates in UTC and chapter numbers") {
    CHECK(ui::chapterDate(0).empty());
    CHECK(ui::chapterDate(-5).empty());
    CHECK(ui::chapterDate(1) == "1970-01-01");
    CHECK(ui::chapterDate(951782400000LL) == "2000-02-29");  // a leap day
    CHECK(ui::chapterDate(1761696000000LL) == "2025-10-29");
    CHECK(ui::chapterDate(1761782399999LL) == "2025-10-29");  // the last millisecond of that day
    CHECK(ui::chapterDate(253402300800000LL).empty());        // the year 10000
    CHECK(ui::chapterNumberText(-1).empty());
    CHECK(ui::chapterNumberText(3) == "3");
    CHECK(ui::chapterNumberText(0) == "0");
    CHECK(ui::chapterNumberText(12.5) == "12.5");
    CHECK(ui::chapterNumberText(7.25) == "7.25");
    CHECK(ui::chapterNumberText(1e12).empty());
    CHECK(ui::chapterSortName(ChapterSort::OldestFirst) == "oldest");
    CHECK(ui::chapterFilterName(ChapterFilter::Bookmarked) == "bookmarked");
}

TEST_CASE("manga: details and all 300 chapters, a page of 100 at a time; newest first") {
    Harness h;
    h.http.hold(true);
    auto model = h.model();
    model->start();
    CHECK(model->state() == State::Loading);
    CHECK(model->manga().title == "Delta Big");  // the library's copy shows at once
    CHECK_FALSE(model->detailsLoaded());

    // Manga and the first Chapters page are asked at the same time. The details alone aren't enough.
    REQUIRE(h.http.waitingCount() == 2);
    REQUIRE(h.http.releaseNext());
    CHECK(model->detailsLoaded());
    CHECK(model->manga().author == "Someone");
    CHECK(model->state() == State::Loading);
    REQUIRE(h.http.releaseNext());
    CHECK(model->state() == State::Ready);
    CHECK(model->items().size() == 100);
    CHECK(model->totalCount() == 300);
    CHECK_FALSE(model->complete());
    const auto revision = model->listRevision();

    REQUIRE(h.http.releaseNext());  // page 2
    CHECK(model->items().size() == 200);
    REQUIRE(h.http.releaseNext());  // page 3
    CHECK_FALSE(h.http.releaseNext());
    CHECK(model->items().size() == 300);
    CHECK(model->complete());
    CHECK(model->listRevision() == revision);  // only added at the end: the rows on screen stay
    CHECK(model->items().front().sourceOrder == 300);
    CHECK(model->items().back().sourceOrder == 1);
    CHECK(h.asked("Chapters") == 3);
    CHECK(h.lastVariables("Chapters")["offset"] == 200);
    CHECK(h.lastVariables("Chapters")["order"] == "DESC");
    CHECK(model->continuePoint().kind == ContinuePoint::Kind::Start);
    CHECK(model->continuePoint().chapterId == 1001);
    CHECK(model->manga().unreadCount == 300);

    model->start();  // only once
    CHECK(h.http.releaseAll() == 0);
}

TEST_CASE("manga: sort and filter on the console; the focus follows its chapter") {
    Harness h(10);
    h.server.chapters[2].isRead = true;  // chapter 3
    h.server.chapters[4].isBookmarked = true;  // chapter 5
    auto model = h.model();
    model->start();
    REQUIRE(model->state() == State::Ready);
    REQUIRE(model->complete());
    model->rememberFocus(indexOfOrder(*model, 7));

    auto revision = model->listRevision();
    model->setSort(ChapterSort::OldestFirst);
    CHECK(model->listRevision() > revision);
    CHECK(sourceOrders(model->items()) == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    CHECK(model->rememberedFocus() == 6);  // chapter 7

    model->setFilter(ChapterFilter::Unread);
    CHECK(sourceOrders(model->items()) == std::vector<int>{1, 2, 4, 5, 6, 7, 8, 9, 10});
    CHECK(model->rememberedFocus() == 5);
    model->setFilter(ChapterFilter::Bookmarked);
    CHECK(sourceOrders(model->items()) == std::vector<int>{5});
    CHECK(model->rememberedFocus() == 0);  // chapter 7 isn't shown: the nearest index
    model->setSort(ChapterSort::NewestFirst);
    model->setFilter(ChapterFilter::All);
    CHECK(model->items().front().sourceOrder == 10);
    CHECK(model->rememberedFocus() == 3);  // chapter 7 again
    CHECK(h.asked("Chapters") == 1);       // no new requests for any of it

    revision = model->listRevision();
    model->setSort(ChapterSort::NewestFirst);  // no change
    CHECK(model->listRevision() == revision);
}

TEST_CASE("manga: mark read and unread, one chapter; the server's answer shows; the read toggle") {
    Harness h(5);
    auto model = h.model();
    model->start();
    REQUIRE(model->complete());
    const std::size_t third = indexOfOrder(*model, 3);
    CHECK(model->toggleReadAction(third) == Action::MarkRead);
    CHECK(model->actionsFor(third) ==
          std::vector<Action>{Action::MarkRead, Action::Bookmark, Action::MarkPreviousRead, Action::MarkPreviousUnread});

    const auto items = model->itemsRevision();
    const auto list = model->listRevision();
    REQUIRE(model->act(third, Action::MarkRead));
    CHECK(h.lastVariables("UpdateChapters") == nlohmann::json::parse(R"({"ids": [1003], "patch": {"isRead": true}})"));
    CHECK(model->items()[third].isRead);
    CHECK(model->itemsRevision() > items);  // redraw in place
    CHECK(model->listRevision() == list);
    CHECK(model->manga().unreadCount == 4);
    CHECK(model->toggleReadAction(third) == Action::MarkUnread);
    CHECK(model->actionsFor(third)[0] == Action::MarkUnread);
    CHECK_FALSE(model->busy());

    CHECK_FALSE(model->act(third, Action::MarkRead));  // already read: nothing to send
    CHECK(h.asked("UpdateChapters") == 1);

    // Unread also goes back to the first page.
    REQUIRE(model->act(third, Action::MarkUnread));
    CHECK(h.lastVariables("UpdateChapters") ==
          nlohmann::json::parse(R"({"ids": [1003], "patch": {"isRead": false, "lastPageRead": 0}})"));
    CHECK_FALSE(model->items()[third].isRead);
    CHECK(model->manga().unreadCount == 5);

    // An unread chapter with progress can be "marked unread": back to page 0.
    h.server.chapters[1].lastPageRead = 4;
    model->reloadChapters();
    const std::size_t second = indexOfOrder(*model, 2);
    CHECK(model->items()[second].lastPageRead == 4);
    CHECK(model->toggleReadAction(second) == Action::MarkRead);
    REQUIRE(model->act(second, Action::MarkUnread));
    CHECK(model->items()[second].lastPageRead == 0);

    CHECK_FALSE(model->act(99, Action::MarkRead));  // no such row
    CHECK(model->actionsFor(99).empty());
}

TEST_CASE("manga: bookmark, and mark all previous read / unread (only what changes is sent)") {
    Harness h(300);
    h.server.chapters[9].isRead = true;  // chapter 10 already read
    auto model = h.model();
    model->start();
    REQUIRE(model->complete());

    const std::size_t at150 = indexOfOrder(*model, 150);
    REQUIRE(model->act(at150, Action::Bookmark));
    CHECK(h.lastVariables("UpdateChapters")["patch"] == nlohmann::json::parse(R"({"isBookmarked": true})"));
    CHECK(model->items()[at150].isBookmarked);
    CHECK(model->actionsFor(at150)[1] == Action::RemoveBookmark);
    CHECK_FALSE(model->act(at150, Action::Bookmark));

    REQUIRE(model->act(at150, Action::MarkPreviousRead));
    const nlohmann::json sent = h.lastVariables("UpdateChapters");
    CHECK(sent["ids"].size() == 148);  // chapters 1..149 without chapter 10
    CHECK(sent["patch"] == nlohmann::json::parse(R"({"isRead": true})"));
    CHECK_FALSE(model->items()[at150].isRead);  // not the chapter itself
    CHECK(model->manga().unreadCount == 151);
    const ContinuePoint next = model->continuePoint();
    CHECK(next.kind == ContinuePoint::Kind::Continue);
    CHECK(next.chapterId == 1150);
    CHECK_FALSE(model->act(at150, Action::MarkPreviousRead));  // nothing left to mark

    // Unread for everything before chapter 11: 10 chapters.
    REQUIRE(model->act(indexOfOrder(*model, 11), Action::MarkPreviousUnread));
    CHECK(h.lastVariables("UpdateChapters")["ids"].size() == 10);
    CHECK(h.lastVariables("UpdateChapters")["patch"] ==
          nlohmann::json::parse(R"({"isRead": false, "lastPageRead": 0})"));
    CHECK(model->manga().unreadCount == 161);
    CHECK_FALSE(model->act(indexOfOrder(*model, 1), Action::MarkPreviousRead));  // nothing before chapter 1

    REQUIRE(model->act(at150, Action::RemoveBookmark));
    CHECK_FALSE(model->items()[at150].isBookmarked);
}

TEST_CASE("manga: a chapter that leaves the filter reloads the list; one action at a time; a failed action") {
    Harness h(5);
    auto model = h.model();
    model->start();
    model->setFilter(ChapterFilter::Unread);
    REQUIRE(model->items().size() == 5);

    h.http.hold(true);
    const auto list = model->listRevision();
    REQUIRE(model->act(0, Action::MarkRead));  // chapter 5
    CHECK(model->busy());
    CHECK_FALSE(model->act(1, Action::MarkRead));  // the first one is still under way
    model->reloadChapters();                       // nor a reload
    model->refreshFromSource();                    // nor a refresh
    CHECK(h.http.releaseAll() == 1);
    CHECK_FALSE(model->busy());
    CHECK(sourceOrders(model->items()) == std::vector<int>{4, 3, 2, 1});  // chapter 5 left the "unread" filter
    CHECK(model->listRevision() > list);

    h.http.hold(false);
    h.server.failUpdate = true;
    REQUIRE(model->act(0, Action::MarkRead));
    const auto error = model->takeActionError();
    REQUIRE(error.has_value());
    CHECK(error->kind == api::ApiErrorKind::Server);
    CHECK_FALSE(model->takeActionError().has_value());  // taken
    CHECK(sourceOrders(model->items()) == std::vector<int>{4, 3, 2, 1});  // unchanged
    CHECK_FALSE(model->busy());
}

TEST_CASE("manga: failures while loading") {
    SUBCASE("the details fail: Failed, Retry loads everything again") {
        Harness h(5);
        h.server.failManga = true;
        auto model = h.model();
        model->start();
        CHECK(model->state() == State::Failed);
        CHECK(model->error().kind == api::ApiErrorKind::Server);
        CHECK(model->items().empty());
        h.server.failManga = false;
        model->retry();
        CHECK(model->state() == State::Ready);
        CHECK(model->items().size() == 5);
    }
    SUBCASE("the first chapter page fails: Failed") {
        Harness h(5);
        h.server.failChaptersPage = 0;
        auto model = h.model();
        model->start();
        CHECK(model->state() == State::Failed);
        h.server.failChaptersPage = -1;
        model->retry();
        CHECK(model->state() == State::Ready);
    }
    SUBCASE("a later page fails: the list keeps what it has, Retry carries on") {
        Harness h(300);
        h.server.failChaptersPage = 1;
        auto model = h.model();
        model->start();
        CHECK(model->state() == State::Ready);
        CHECK(model->moreFailed());
        CHECK_FALSE(model->complete());
        CHECK(model->items().size() == 100);
        h.server.failChaptersPage = -1;
        model->retry();
        CHECK_FALSE(model->moreFailed());
        CHECK(model->complete());
        CHECK(model->items().size() == 300);
        CHECK(h.asked("Manga") == 1);  // the details weren't asked again
    }
    SUBCASE("no chapters: Empty") {
        Harness h(0);
        auto model = h.model();
        model->start();
        CHECK(model->state() == State::Empty);
        CHECK(model->continuePoint().kind == ContinuePoint::Kind::None);
    }
    SUBCASE("destroyed while loading and while an action is under way: nothing arrives late") {
        Harness h(300);
        h.http.hold(true);
        auto model = h.model();
        model->start();
        model.reset();
        h.http.releaseAll();
        CHECK(h.changes == 1);  // only the "loading" of start()

        h.http.hold(false);
        auto second = h.model();
        second->start();
        h.http.hold(true);
        REQUIRE(second->act(0, Action::MarkRead));
        second.reset();
        h.http.releaseAll();  // no crash
    }
}

TEST_CASE("manga: reload keeps the list until the new one is in; refresh from source; the chapter limit") {
    Harness h(250);
    auto model = h.model();
    model->start();
    REQUIRE(model->complete());

    h.server.chapters[0].isRead = true;
    h.http.hold(true);
    model->reloadChapters();
    CHECK(model->busy());
    REQUIRE(h.http.releaseNext());  // page 1 of 3
    CHECK(model->items().size() == 250);
    CHECK_FALSE(model->chapter(1001)->isRead);  // still the old list
    CHECK(h.http.releaseAll() == 2);
    CHECK_FALSE(model->busy());
    CHECK(model->chapter(1001)->isRead);
    CHECK(model->manga().unreadCount == 249);

    // Refresh: the source has one more chapter and a new description.
    h.http.hold(false);
    const auto list = model->listRevision();
    model->refreshFromSource();
    CHECK(h.asked("FetchMangaAndChapters") == 1);
    CHECK(h.lastVariables("FetchMangaAndChapters") == nlohmann::json::parse(R"({"id": 4})"));
    CHECK_FALSE(model->refreshing());
    CHECK(model->manga().description == "Refreshed from the source.");
    CHECK(model->items().size() == 251);
    CHECK(model->items().front().sourceOrder == 251);
    CHECK(model->listRevision() > list);  // a new chapter at the top

    h.server.failRefresh = true;
    model->refreshFromSource();
    CHECK(model->takeActionError().has_value());
    CHECK_FALSE(model->busy());
    CHECK(model->items().size() == 251);

    // A reload whose page fails keeps the old list.
    h.server.failChaptersPage = 2;
    model->reloadChapters();
    CHECK(model->takeActionError().has_value());
    CHECK(model->items().size() == 251);

    // RuntimeProfile::maxChapters: the list stops there.
    Harness capped(300);
    auto small = capped.model(150);
    small->start();
    CHECK(small->complete());
    CHECK(small->items().size() == 150);
    CHECK(capped.asked("Chapters") == 2);
}

}  // TEST_SUITE
