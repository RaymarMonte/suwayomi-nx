// WP-15: the Updates and History tabs' models (src/ui/updates/ChapterRowsModel, LibraryUpdater) on a FakeHttpClient.
// The fake client answers inside the call, so loads are synchronous; the library update's polls go through a played
// schedule and clock.
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "ApiTestKit.hpp"
#include "api/SuwayomiClient.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/updates/ChapterRowsModel.hpp"
#include "ui/updates/LibraryUpdater.hpp"

using namespace snx;
using net::FakeHttpClient;
using net::Request;
using ui::ChapterRowsModel;
using ui::LibraryUpdater;

namespace {

constexpr std::int64_t kNow = 1763035200;  // 2025-11-13 12:00 UTC, seconds
constexpr std::int64_t kDay = 86400;

struct Row {
    int chapterId;
    int mangaId;
    std::int64_t fetchedAt;
    std::int64_t inLibraryAt;
    int lastPageRead = 0;
    bool isRead = false;
};

// A server: the chapter list the Updates / History query pages through (already in the server's order), the
// library update's statuses (one per poll, the last one repeats), and chapter actions.
struct FakeServer {
    std::vector<Row> rows;
    std::deque<nlohmann::json> statuses;  // {lastUpdateTimestamp, libraryUpdateStatus}
    nlohmann::json lastStatus;
    bool failRows = false;
    bool failStatus = false;
    bool insertBeforeNextPage = false;  // a new chapter arrives between two page requests

    static nlohmann::json status(std::int64_t timestampMs, bool running, int total, int finished, int skipped,
                                 nlohmann::json mangaUpdates = nlohmann::json::array()) {
        return {{"lastUpdateTimestamp", {{"timestamp", std::to_string(timestampMs)}}},
                {"libraryUpdateStatus",
                 {{"jobsInfo",
                   {{"isRunning", running}, {"totalJobs", total}, {"finishedJobs", finished},
                    {"skippedMangasCount", skipped}, {"skippedCategoriesCount", 0}}},
                  {"mangaUpdates", mangaUpdates}}}};
    }

    net::HttpResult answer(const Request& request) {
        const std::string operation = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (operation == "LibraryUpdates" || operation == "ReadingHistory") {
            if (failRows) {
                return FakeHttpClient::answer(500, "oops");
            }
            const int first = vars["first"].get<int>();
            const int offset = vars["offset"].get<int>();
            if (insertBeforeNextPage && offset > 0) {
                insertBeforeNextPage = false;
                Row fresh = rows.front();
                fresh.chapterId = 999;
                rows.insert(rows.begin(), fresh);  // every later row moves one down: page 2 repeats one of page 1
            }
            nlohmann::json nodes = nlohmann::json::array();
            for (int i = offset; i < offset + first && i < static_cast<int>(rows.size()); ++i) {
                const Row& r = rows[static_cast<std::size_t>(i)];
                nodes.push_back({{"id", r.chapterId}, {"mangaId", r.mangaId}, {"name", "Chapter " + std::to_string(r.chapterId)},
                                 {"fetchedAt", std::to_string(r.fetchedAt)}, {"lastPageRead", r.lastPageRead},
                                 {"isRead", r.isRead}, {"lastReadAt", std::to_string(r.fetchedAt)},
                                 {"manga", {{"id", r.mangaId}, {"title", "Manga " + std::to_string(r.mangaId)},
                                            {"inLibraryAt", std::to_string(r.inLibraryAt)}, {"inLibrary", true}}}});
            }
            const bool more = offset + first < static_cast<int>(rows.size());
            return FakeHttpClient::answer(
                200, nlohmann::json{{"data", {{"chapters", {{"nodes", nodes}, {"totalCount", rows.size()},
                                                            {"pageInfo", {{"hasNextPage", more}}}}}}}}.dump());
        }
        if (operation == "UpdateChapters") {
            nlohmann::json changed = nlohmann::json::array();
            for (const auto& id : vars["ids"]) {
                for (Row& r : rows) {
                    if (r.chapterId == id.get<int>()) {
                        if (vars["patch"].contains("isRead")) r.isRead = vars["patch"]["isRead"].get<bool>();
                        if (vars["patch"].contains("lastPageRead")) r.lastPageRead = vars["patch"]["lastPageRead"].get<int>();
                        changed.push_back({{"id", r.chapterId}, {"isRead", r.isRead}, {"isBookmarked", false},
                                           {"lastPageRead", r.lastPageRead}, {"pageCount", 5}, {"lastReadAt", "77"}});
                    }
                }
            }
            return FakeHttpClient::answer(200, nlohmann::json{{"data", {{"updateChapters", {{"chapters", changed}}}}}}.dump());
        }
        if (operation == "Manga") {
            return FakeHttpClient::answer(
                200, nlohmann::json{{"data", {{"manga", {{"id", vars["id"]}, {"title", "M"}, {"unreadCount", 4}}}}}}.dump());
        }
        if (operation == "LibraryUpdateStatus") {
            if (failStatus) {
                return FakeHttpClient::answer(503, "away");
            }
            if (!statuses.empty()) {
                lastStatus = statuses.front();
                statuses.pop_front();
            }
            return FakeHttpClient::answer(200, nlohmann::json{{"data", lastStatus}}.dump());
        }
        if (operation == "UpdateLibrary") {
            return FakeHttpClient::answer(200, R"({"data":{"updateLibrary":{"clientMutationId":null}}})");
        }
        if (operation == "StopLibraryUpdate") {
            return FakeHttpClient::answer(200, R"({"data":{"updateStop":{"clientMutationId":null}}})");
        }
        return FakeHttpClient::answer(404);
    }
};

struct Harness {
    FakeServer server;
    FakeHttpClient http;
    std::unique_ptr<api::SuwayomiClient> client;
    int changes = 0;
    // The played schedule and clock (milliseconds).
    std::int64_t now = 0;
    std::vector<std::pair<int, std::function<void()>>> scheduled;

    Harness() {
        server.lastStatus = FakeServer::status(0, false, 0, 0, 0);
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = "http://192.168.1.10:4567";
        api::ClientOptions options;
        options.updatesPageSize = 4;  // small pages, so paging shows with a few rows
        options.historyPageSize = 4;
        client = std::make_unique<api::SuwayomiClient>(http, settings, options);
    }

    std::unique_ptr<ChapterRowsModel> rows(ChapterRowsModel::Kind kind, std::size_t maxRows = 100, int fillPages = 2,
                                           std::size_t screenRows = 3) {
        ChapterRowsModel::Limits limits;
        limits.fillPages = fillPages;
        limits.maxRows = maxRows;
        limits.screenRows = screenRows;
        auto made = std::make_unique<ChapterRowsModel>(*client, kind, limits);
        made->setListener([this] { ++changes; });
        return made;
    }

    std::unique_ptr<LibraryUpdater> updater() {
        LibraryUpdater::Limits limits;  // poll 1 s, start wait 10 s, retry 5 s .. 60 s
        auto made = std::make_unique<LibraryUpdater>(
            *client, limits, [this] { return now; },
            [this](int ms, std::function<void()> fn) { scheduled.emplace_back(ms, std::move(fn)); });
        made->addListener([this] { ++changes; });
        return made;
    }

    // Runs the next scheduled task, the clock moved on by its delay. Returns the delay (-1: nothing scheduled).
    int runNext() {
        if (scheduled.empty()) {
            return -1;
        }
        auto [ms, fn] = std::move(scheduled.front());
        scheduled.erase(scheduled.begin());
        now += ms;
        fn();
        return ms;
    }

    int asked(const std::string& operation) const {
        int n = 0;
        for (const Request& r : http.requests()) {
            n += operationOf(r) == operation ? 1 : 0;
        }
        return n;
    }
};

// `count` rows newest first, one minute apart; every `newEvery`-th is new (fetched after its manga joined), the others
// came with their manga.
std::vector<Row> updateRows(int count, int newEvery) {
    std::vector<Row> out;
    for (int i = 0; i < count; ++i) {
        const std::int64_t fetched = kNow - i * 60;
        const bool isNew = i % newEvery == 0;
        out.push_back({100 + i, 1 + i % 5, fetched, isNew ? fetched - kDay : fetched});
    }
    return out;
}

std::vector<int> chapterIds(const std::vector<core::ChapterEntry>& items) {
    std::vector<int> ids;
    for (const auto& e : items) {
        ids.push_back(e.chapter.id);
    }
    return ids;
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("updates: pages with hidden rows fill up to a screen, at most fillPages in a row, then wait for the scroll") {
    Harness h;
    h.server.rows = updateRows(40, 4);  // 4 per page, 1 of them new
    auto model = h.rows(ChapterRowsModel::Kind::Updates);  // a screen = 3 rows, 2 pages per run
    model->start();
    CHECK(model->state() == ChapterRowsModel::State::Ready);
    CHECK(model->pages() == 2);  // stopped after 2 pages with 2 rows (less than a screen)
    CHECK(chapterIds(model->items()) == std::vector<int>{100, 104});
    CHECK(model->hidden() == 6);
    CHECK(model->newestFetchedAt() == kNow);
    CHECK_FALSE(model->ended());
    // The view shows row 0 only: still near the end (within a screen): the next run.
    model->showing(0);
    CHECK(model->pages() == 4);
    CHECK(model->items().size() == 4);
    // Not near the end: nothing more.
    h.server.rows = updateRows(40, 1);
    const int before = h.asked("LibraryUpdates");
    model->showing(0);  // 0 + 3 < 4: not near the end
    CHECK(h.asked("LibraryUpdates") == before);
}

TEST_CASE("updates: the 90-day window and the row cap end the list; the server's end too") {
    Harness h;
    h.server.rows = updateRows(6, 1);
    h.server.rows[3].fetchedAt = kNow - 91 * kDay;  // older than the window: the end
    h.server.rows[3].inLibraryAt = 0;
    auto model = h.rows(ChapterRowsModel::Kind::Updates, 100, 5, 10);
    model->start();
    CHECK(chapterIds(model->items()) == std::vector<int>{100, 101, 102});
    CHECK(model->ended());
    CHECK(h.asked("LibraryUpdates") == 1);

    Harness capped;
    capped.server.rows = updateRows(20, 1);
    auto small = capped.rows(ChapterRowsModel::Kind::Updates, 5, 5, 10);
    small->start();
    CHECK(small->items().size() == 5);
    CHECK(small->ended());

    Harness shortList;
    shortList.server.rows = updateRows(3, 1);
    auto all = shortList.rows(ChapterRowsModel::Kind::Updates, 100, 5, 10);
    all->start();
    CHECK(all->items().size() == 3);
    CHECK(all->ended());  // hasNextPage false
}

TEST_CASE("updates: a chapter seen on two pages shows once; an all-hidden list keeps loading, then Empty") {
    Harness h;
    h.server.rows = updateRows(8, 1);
    h.server.insertBeforeNextPage = true;
    auto model = h.rows(ChapterRowsModel::Kind::Updates, 100, 5, 10);
    model->start();
    const std::vector<int> ids = chapterIds(model->items());
    CHECK(std::count(ids.begin(), ids.end(), 103) == 1);  // page 2 began with the last row of page 1 again
    CHECK(model->items().size() == 8);

    Harness old;
    old.server.rows = updateRows(20, 1000);  // only the first is new... make none new:
    old.server.rows[0].inLibraryAt = old.server.rows[0].fetchedAt;
    auto empty = old.rows(ChapterRowsModel::Kind::Updates, 100, 2, 3);
    empty->start();
    CHECK(empty->pages() == 5);  // past fillPages: an empty list has nothing to scroll to
    CHECK(empty->state() == ChapterRowsModel::State::Empty);
}

TEST_CASE("history: one row per manga across pages, the most recent chapter of each") {
    Harness h;
    // Manga 1 three times, then 2, then 1 again on the next page, then 3.
    h.server.rows = {{11, 1, kNow, 0, 4}, {12, 1, kNow - 1, 0, 2}, {13, 1, kNow - 2, 0, 0, true}, {21, 2, kNow - 3, 0, 1},
                     {14, 1, kNow - 4, 0, 1}, {31, 3, kNow - 5, 0, 0, true}};
    auto model = h.rows(ChapterRowsModel::Kind::History, 100, 5, 10);
    model->start();
    CHECK(chapterIds(model->items()) == std::vector<int>{11, 21, 31});
    CHECK(model->items()[0].chapter.lastPageRead == 4);
    CHECK(model->hidden() == 3);
    CHECK(model->ended());
}

TEST_CASE("rows: a reload keeps the rows until the new first page; a failed reload keeps them and says why") {
    Harness h;
    h.server.rows = updateRows(3, 1);
    auto model = h.rows(ChapterRowsModel::Kind::Updates, 100, 5, 10);
    model->start();
    const auto revision = model->listRevision();
    h.server.rows.insert(h.server.rows.begin(), Row{200, 9, kNow + 60, 0});
    model->reload();
    CHECK(model->items().front().chapter.id == 200);
    CHECK(model->listRevision() > revision);

    h.server.failRows = true;
    model->reload();
    CHECK(model->items().size() == 4);
    CHECK(model->state() == ChapterRowsModel::State::Ready);
    CHECK(model->takeError().has_value());
    CHECK_FALSE(model->takeError().has_value());

    Harness first;
    first.server.failRows = true;
    auto failed = first.rows(ChapterRowsModel::Kind::History);
    failed->start();
    CHECK(failed->state() == ChapterRowsModel::State::Failed);
    CHECK(failed->error().kind == api::ApiErrorKind::Server);
}

TEST_CASE("rows: X marks read, again unread at page 0; the row shows the server's answer, the library hears of it") {
    Harness h;
    h.server.rows = updateRows(2, 1);
    h.server.rows[1].lastPageRead = 3;
    auto model = h.rows(ChapterRowsModel::Kind::Updates, 100, 5, 10);
    std::vector<int> changedManga;
    model->setMangaChanged([&](const core::Manga& manga) { changedManga.push_back(manga.id); });
    model->start();
    auto lastPatch = [&h] {
        nlohmann::json patch;
        for (const Request& r : h.http.requests()) {
            if (operationOf(r) == "UpdateChapters") {
                patch = variablesOf(r)["patch"];
            }
        }
        return patch;
    };
    REQUIRE(model->toggleRead(0));
    CHECK(model->items()[0].chapter.isRead);
    CHECK(lastPatch() == nlohmann::json{{"isRead", true}});
    CHECK(changedManga == std::vector<int>{model->items()[0].manga.id});
    // A chapter with progress toggles to unread (back to page 0, D43).
    REQUIRE(model->toggleRead(1));
    CHECK(lastPatch() == nlohmann::json{{"isRead", false}, {"lastPageRead", 0}});
    CHECK(model->items()[1].chapter.lastPageRead == 0);
    CHECK_FALSE(model->items()[1].chapter.isRead);
    // Nothing to change: no request.
    const int asked = h.asked("UpdateChapters");
    CHECK_FALSE(model->setRead(0, true));
    CHECK_FALSE(model->setRead(9, true));
    CHECK(h.asked("UpdateChapters") == asked);
}

TEST_CASE("library update: start reads the timestamp first, polls while it runs, then the summary") {
    Harness h;
    const nlohmann::json failed = nlohmann::json::array({{{"status", "FAILED"}, {"manga", {{"id", 3}, {"title", "Gone"}}}}});
    h.server.statuses = {
        FakeServer::status(1000, false, 0, 0, 0),         // before
        FakeServer::status(kNow * 1000, false, 0, 0, 0),  // the race: moved, the old counts
        FakeServer::status(kNow * 1000, true, 2, 1, 154),
        FakeServer::status(kNow * 1000, false, 2, 2, 154),
        FakeServer::status(kNow * 1000, false, 2, 2, 154, failed),  // withManga
    };
    h.server.rows = updateRows(4, 2);  // rows 0 and 2 are new, fetched at kNow and kNow - 120
    h.server.rows[2].fetchedAt = kNow - 5;
    auto updater = h.updater();
    REQUIRE(updater->start());
    CHECK_FALSE(updater->start());  // one at a time
    CHECK(h.asked("UpdateLibrary") == 1);
    CHECK(updater->phase() == LibraryUpdater::Phase::Starting);
    CHECK(h.runNext() == 1000);
    CHECK(updater->phase() == LibraryUpdater::Phase::Starting);  // the race: not done
    h.runNext();
    CHECK(updater->phase() == LibraryUpdater::Phase::Running);
    CHECK(updater->lastStatus()->finished == 1);
    h.runNext();
    REQUIRE(updater->phase() == LibraryUpdater::Phase::Finished);
    REQUIRE(updater->summary().has_value());
    const LibraryUpdater::Summary& s = *updater->summary();
    CHECK(s.newChapters == 1);  // only row 0 was fetched since the update started (row 2 is 5 s older)
    CHECK(s.skippedManga == 154);
    CHECK(s.total == 2);
    CHECK(s.failed == std::vector<std::string>{"Gone"});
    CHECK_FALSE(s.stopped);
    CHECK_FALSE(s.nothingToUpdate);
    CHECK(s.ms == 3000);
    CHECK(updater->finishedCount() == 1);
    CHECK(h.scheduled.empty());  // no more polls once done
}

TEST_CASE("library update: nothing seen within the start wait is 'nothing to update'; Stop sends updateStop") {
    Harness h;
    h.server.statuses = {FakeServer::status(5000, false, 3, 3, 0)};  // never changes
    auto updater = h.updater();
    REQUIRE(updater->start());
    int polls = 0;
    while (updater->active() && polls < 20) {
        h.runNext();
        ++polls;
    }
    CHECK(polls == 10);  // 10 s of 1 s polls
    REQUIRE(updater->summary().has_value());
    CHECK(updater->summary()->nothingToUpdate);

    Harness s;
    s.server.statuses = {FakeServer::status(1, false, 0, 0, 0), FakeServer::status(2, true, 40, 3, 0),
                         FakeServer::status(2, false, 40, 9, 0)};
    auto stopping = s.updater();
    CHECK_FALSE(stopping->stop());  // nothing runs
    stopping->start();
    s.runNext();
    CHECK(stopping->phase() == LibraryUpdater::Phase::Running);
    REQUIRE(stopping->stop());
    CHECK(s.asked("StopLibraryUpdate") == 1);
    CHECK_FALSE(stopping->stop());  // already sent
    s.runNext();
    REQUIRE(stopping->summary().has_value());
    CHECK(stopping->summary()->stopped);
}

TEST_CASE("library update: a failed poll says so and waits longer each time; check() follows an update already running") {
    Harness h;
    h.server.statuses = {FakeServer::status(1, false, 0, 0, 0), FakeServer::status(2, true, 5, 1, 0)};
    auto updater = h.updater();
    updater->start();
    h.runNext();
    CHECK(updater->phase() == LibraryUpdater::Phase::Running);
    h.server.failStatus = true;
    CHECK(h.runNext() == 1000);
    CHECK(updater->pollFailed());
    CHECK(h.runNext() == 5000);  // progressSyncMs
    CHECK(h.runNext() == 10000);  // doubled
    h.server.failStatus = false;
    h.server.statuses = {FakeServer::status(2, false, 5, 5, 0)};
    CHECK(h.runNext() == 20000);
    CHECK_FALSE(updater->pollFailed());
    CHECK(updater->phase() == LibraryUpdater::Phase::Finished);

    Harness other;
    other.server.statuses = {FakeServer::status(7, true, 10, 2, 0), FakeServer::status(7, false, 10, 10, 0)};
    auto follower = other.updater();
    follower->check();
    CHECK(follower->phase() == LibraryUpdater::Phase::Running);
    other.runNext();
    CHECK(follower->phase() == LibraryUpdater::Phase::Finished);
    CHECK(other.asked("UpdateLibrary") == 0);

    Harness idle;
    idle.server.statuses = {FakeServer::status(123000, false, 0, 0, 0)};
    auto quiet = idle.updater();
    quiet->check();
    CHECK(quiet->phase() == LibraryUpdater::Phase::Idle);
    CHECK(quiet->lastStatus()->lastUpdateMs == 123000);
    CHECK(idle.scheduled.empty());
}

TEST_CASE("library update: a scheduled poll after the updater is gone does nothing") {
    Harness h;
    h.server.statuses = {FakeServer::status(1, false, 0, 0, 0)};
    auto updater = h.updater();
    updater->start();
    REQUIRE(h.scheduled.size() == 1);
    updater.reset();
    const int asked = h.asked("LibraryUpdateStatus");
    h.runNext();
    CHECK(h.asked("LibraryUpdateStatus") == asked);
}

}  // TEST_SUITE("ui")
