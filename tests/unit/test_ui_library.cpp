// WP-09: the library screen's model (src/ui/library/LibraryModel) on a FakeHttpClient, and the grid arithmetic
// (src/ui/components/GridLayout). The fake server below answers Categories and LibraryByCategory from a small in-memory
// library, paged like the real one.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "ApiTestKit.hpp"
#include "api/SuwayomiClient.hpp"
#include "core/AppConfig.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/components/GridLayout.hpp"
#include "ui/library/LibraryModel.hpp"

using namespace snx;
using core::LibrarySort;
using net::FakeHttpClient;
using net::Request;
using ui::GridLayout;
using ui::GridMove;
using ui::LibraryModel;

namespace {

struct FakeManga {
    int id;
    std::string title;
    int unread;
    long long lastReadAt;
};

// A server with categories (id -> name, in order) and the manga in each (in title order, as the server sorts).
struct FakeLibrary {
    std::vector<std::pair<int, std::string>> categories{{0, "Default"}, {1, "Reading"}, {2, "Done"}};
    std::map<int, std::vector<FakeManga>> manga;
    bool failLibrary = false;

    net::HttpResult answer(const Request& request) const {
        const std::string operation = operationOf(request);
        if (operation == "Categories") {
            nlohmann::json nodes = nlohmann::json::array();
            int order = 0;
            for (const auto& [id, name] : categories) {
                nodes.push_back({{"id", id}, {"name", name}, {"order", order++}, {"default", id == 0}});
            }
            return FakeHttpClient::answer(
                200, nlohmann::json{{"data", {{"categories", {{"nodes", nodes}, {"totalCount", nodes.size()}}}}}}.dump());
        }
        if (operation == "LibraryByCategory") {
            if (failLibrary) {
                return FakeHttpClient::answer(500, "oops");
            }
            const nlohmann::json vars = variablesOf(request);
            const nlohmann::json& filter = vars["filter"]["categoryId"];
            const int category = filter.contains("equalTo") ? filter["equalTo"].get<int>() : 0;
            const int first = vars["first"].get<int>();
            const int offset = vars["offset"].get<int>();
            const auto found = manga.find(category);
            const std::vector<FakeManga> none;
            const std::vector<FakeManga>& all = found == manga.end() ? none : found->second;
            nlohmann::json nodes = nlohmann::json::array();
            for (int i = offset; i < offset + first && i < static_cast<int>(all.size()); ++i) {
                const FakeManga& m = all[static_cast<std::size_t>(i)];
                nlohmann::json node = {{"id", m.id}, {"title", m.title}, {"thumbnailUrl", "/api/v1/manga/" + std::to_string(m.id) + "/thumbnail"},
                                       {"status", "ONGOING"}, {"unreadCount", m.unread}, {"downloadCount", 0}};
                node["lastReadChapter"] = m.lastReadAt == 0 ? nlohmann::json(nullptr)
                                                            : nlohmann::json{{"lastReadAt", std::to_string(m.lastReadAt)}};
                nodes.push_back(node);
            }
            const bool more = offset + first < static_cast<int>(all.size());
            return FakeHttpClient::answer(
                200, nlohmann::json{{"data", {{"mangas", {{"nodes", nodes}, {"totalCount", all.size()},
                                                          {"pageInfo", {{"hasNextPage", more}, {"endCursor", "x"}}}}}}}}
                         .dump());
        }
        return FakeHttpClient::answer(404);
    }
};

// The model on a fake client whose calls complete inside the call (no deliver), so every step is synchronous.
struct Harness {
    FakeLibrary library;
    FakeHttpClient http;
    std::unique_ptr<api::SuwayomiClient> client;
    int changes = 0;

    Harness() {
        http.setHandler([this](const Request& request) { return library.answer(request); });
        core::ServerSettings server;
        server.url = "http://192.168.1.10:4567";
        api::ClientOptions options;
        options.libraryPageSize = 4;  // small pages, so paging shows with a few manga
        client = std::make_unique<api::SuwayomiClient>(http, server, options);
    }

    std::unique_ptr<LibraryModel> model(core::LibrarySettings start = {}) {
        auto made = std::make_unique<LibraryModel>(*client, start, 4);
        made->setListener([this] { ++changes; });
        return made;
    }

    int pagesAsked() const {
        int n = 0;
        for (const Request& r : http.requests()) {
            n += operationOf(r) == "LibraryByCategory" ? 1 : 0;
        }
        return n;
    }
};

std::vector<FakeManga> series(int firstId, int count) {
    std::vector<FakeManga> list;
    for (int i = 0; i < count; ++i) {
        const int id = firstId + i;
        list.push_back({id, "Manga " + std::to_string(100 + i), (i * 7) % 5, i % 3 == 0 ? 0 : 1000 + i});
    }
    return list;
}

std::vector<int> ids(const std::vector<core::Manga>& list) {
    std::vector<int> out;
    for (const auto& m : list) {
        out.push_back(m.id);
    }
    return out;
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("library: categories, an empty Default isn't a tab, the start category comes back") {
    Harness h;
    h.library.manga[1] = series(10, 3);
    h.library.manga[2] = series(20, 2);
    core::LibrarySettings start;
    start.categoryId = 2;
    auto model = h.model(start);
    CHECK(model->state() == LibraryModel::State::Loading);
    model->start();
    REQUIRE(model->categories().size() == 2);  // Default holds nothing
    CHECK(model->categories()[0].name == "Reading");
    CHECK(model->category()->id == 2);
    CHECK(model->state() == LibraryModel::State::Ready);
    CHECK(ids(model->items()) == std::vector<int>{20, 21});
    CHECK(model->totalCount() == 2);
    CHECK(h.changes > 0);
    model->start();  // only once
    CHECK(h.pagesAsked() == 2);  // Default's probe + category 2

    SUBCASE("a start category that is gone means the first tab") {
        core::LibrarySettings gone;
        gone.categoryId = 99;
        auto other = h.model(gone);
        other->start();
        CHECK(other->category()->id == 1);
        CHECK(other->settings().categoryId == 1);
    }
}

TEST_CASE("library: Default with manga is the first tab; only Default and empty = the library is empty") {
    Harness h;
    h.library.manga[0] = series(1, 2);
    auto model = h.model();
    model->start();
    REQUIRE(model->categories().size() == 3);
    CHECK(model->category()->id == 0);
    CHECK(model->items().size() == 2);
    CHECK(h.pagesAsked() == 1);  // the probe page is the start category's first page

    Harness empty;
    empty.library.categories = {{0, "Default"}};
    auto nothing = empty.model();
    nothing->start();
    CHECK(nothing->state() == LibraryModel::State::Empty);
    CHECK(nothing->libraryEmpty());

    Harness emptyCategory;
    emptyCategory.library.manga[1] = series(10, 1);
    auto someEmpty = emptyCategory.model();
    someEmpty->start();
    someEmpty->selectCategory(1);  // "Done" has none
    CHECK(someEmpty->state() == LibraryModel::State::Empty);
    CHECK_FALSE(someEmpty->libraryEmpty());
}

TEST_CASE("library: title order pages in as the grid nears the end; no duplicates; never past the end") {
    Harness h;
    h.library.manga[1] = series(10, 10);  // pages of 4: 4 + 4 + 2
    auto model = h.model();
    model->start();
    REQUIRE(model->category()->id == 1);
    CHECK(model->items().size() == 4);
    model->showing(0);  // far from the end (4 - 0 > 2)
    CHECK(model->items().size() == 4);
    model->showing(2);  // within half a page of the end
    CHECK(model->items().size() == 8);
    model->showing(7);
    CHECK(model->items().size() == 10);
    const int asked = h.pagesAsked();
    model->showing(9);  // complete: nothing more to ask
    CHECK(h.pagesAsked() == asked);
    CHECK(ids(model->items()).front() == 10);
    CHECK(ids(model->items()).back() == 19);

    SUBCASE("a manga seen twice across pages shows once") {
        Harness d;
        d.library.manga[1] = {{1, "A", 0, 0}, {2, "B", 0, 0}, {3, "C", 0, 0}, {4, "D", 0, 0}, {4, "D", 0, 0}, {5, "E", 0, 0}};
        auto m = d.model();
        m->start();
        m->showing(3);
        CHECK(ids(m->items()) == std::vector<int>{1, 2, 3, 4, 5});
    }
}

TEST_CASE("library: unread and last-read sorts load the whole category first; ties keep the title order") {
    Harness h;
    h.library.manga[1] = {{1, "A", 2, 0}, {2, "B", 5, 300}, {3, "C", 2, 900}, {4, "D", 0, 100}, {5, "E", 5, 0}, {6, "F", 1, 0}};
    core::LibrarySettings start;
    start.sort = LibrarySort::Unread;
    auto model = h.model(start);
    model->start();
    CHECK(model->state() == LibraryModel::State::Ready);
    CHECK(ids(model->items()) == std::vector<int>{2, 5, 1, 3, 6, 4});
    model->setSort(LibrarySort::LastRead);
    CHECK(ids(model->items()) == std::vector<int>{3, 2, 4, 1, 5, 6});
    model->setSort(LibrarySort::Title);
    CHECK(ids(model->items()) == std::vector<int>{1, 2, 3, 4, 5, 6});
    CHECK(model->settings().sort == LibrarySort::Title);

    SUBCASE("switching to a sort that needs everything loads the rest") {
        Harness t;
        t.library.manga[1] = series(10, 10);
        auto m = t.model();
        m->start();
        CHECK(m->items().size() == 4);
        m->setSort(LibrarySort::Unread);
        CHECK(m->items().size() == 10);
        CHECK(m->loadedCount() == 10);
    }
}

TEST_CASE("library: L/R stop at the ends, shelves stay loaded, focus is remembered per category") {
    Harness h;
    h.library.manga[0] = series(1, 2);
    h.library.manga[1] = series(10, 3);
    auto model = h.model();
    model->start();
    model->stepCategory(-1);
    CHECK(model->categoryIndex() == 0);
    model->rememberFocus(1);
    model->stepCategory(1);
    CHECK(model->category()->id == 1);
    CHECK(model->settings().categoryId == 1);
    CHECK(model->rememberedFocus() == 0);
    model->rememberFocus(2);
    model->stepCategory(5);
    CHECK(model->category()->id == 2);
    model->stepCategory(-1);
    CHECK(model->rememberedFocus() == 2);
    const int asked = h.pagesAsked();
    model->stepCategory(-1);
    CHECK(model->rememberedFocus() == 1);
    CHECK(h.pagesAsked() == asked);  // both shelves were loaded already
}

TEST_CASE("library: errors; a later page failing keeps the grid; refresh starts over") {
    Harness h;
    h.library.manga[1] = series(10, 10);
    h.library.failLibrary = true;
    auto model = h.model();
    model->start();
    CHECK(model->state() == LibraryModel::State::Failed);
    CHECK(model->error().kind == api::ApiErrorKind::Server);
    CHECK(model->categories().empty());

    h.library.failLibrary = false;
    model->refresh();
    REQUIRE(model->state() == LibraryModel::State::Ready);
    CHECK(model->items().size() == 4);

    h.library.failLibrary = true;
    model->showing(3);
    CHECK(model->state() == LibraryModel::State::Ready);
    CHECK(model->moreFailed());
    CHECK(model->items().size() == 4);
    const int asked = h.pagesAsked();
    model->showing(3);  // not again by itself
    CHECK(h.pagesAsked() == asked);

    h.library.failLibrary = false;
    h.library.manga[1].push_back({99, "Zed", 0, 0});
    model->refresh();
    model->showing(7);
    model->showing(9);
    CHECK(model->items().size() == 11);
    CHECK_FALSE(model->moreFailed());
}

TEST_CASE("library: a held answer; destroying the model or switching cancels it") {
    Harness h;
    h.library.manga[0] = series(1, 2);
    h.library.manga[1] = series(10, 3);
    h.http.hold(true);
    auto model = h.model();
    model->start();
    CHECK(model->state() == LibraryModel::State::Loading);
    REQUIRE(h.http.releaseNext());  // categories
    REQUIRE(h.http.releaseNext());  // Default's first page
    CHECK(model->state() == LibraryModel::State::Ready);
    model->selectCategory(1);
    CHECK(model->state() == LibraryModel::State::Loading);
    model->selectCategory(0);  // back before category 1 answered: its request is cancelled
    CHECK(model->state() == LibraryModel::State::Ready);
    h.http.releaseAll();
    CHECK(model->category()->id == 0);
    model->selectCategory(1);
    model.reset();      // the tab and its model go away with a request under way
    h.http.releaseAll();  // its answer must not touch the deleted model (ASan would say)
}

TEST_CASE("library: sort names in config.json") {
    CHECK(core::librarySortName(LibrarySort::LastRead) == "last_read");
    CHECK(core::librarySortFromName("unread") == LibrarySort::Unread);
    CHECK_FALSE(core::librarySortFromName("random").has_value());
    core::AppConfig config;
    config.library.categoryId = 7;
    config.library.sort = LibrarySort::Unread;
    const core::ConfigLoad back = core::loadAppConfig(core::saveAppConfig(config));
    CHECK(back.problems.empty());
    CHECK(back.config.library.categoryId == 7);
    CHECK(back.config.library.sort == LibrarySort::Unread);
    const core::ConfigLoad bad = core::loadAppConfig(R"({"schemaVersion":1,"library":{"categoryId":"x","sort":"random"}})");
    CHECK(bad.problems.size() == 2);
    CHECK(bad.config.library.categoryId == 0);
    CHECK(bad.config.library.sort == LibrarySort::Title);
}

TEST_CASE("grid: columns, positions, the visible rows and focus moves") {
    // 5 cells of 160 with 20 between fit in 900 - 2*10 = 880 (5*160 + 4*20 = 880).
    const GridLayout grid(900, {160, 260, 20, 10});
    CHECK(grid.columns() == 5);
    CHECK(GridLayout(899, {160, 260, 20, 10}).columns() == 4);
    CHECK(GridLayout(50, {160, 260, 20, 10}).columns() == 1);
    CHECK(grid.rows(0) == 0);
    CHECK(grid.rows(11) == 3);
    CHECK(grid.contentHeight(0) == doctest::Approx(20));
    CHECK(grid.contentHeight(11) == doctest::Approx(10 + 3 * 260 + 2 * 20 + 10));
    CHECK(grid.origin(0).x == doctest::Approx(10));
    CHECK(grid.origin(6).x == doctest::Approx(10 + 180));
    CHECK(grid.origin(6).y == doctest::Approx(10 + 280));
    CHECK(GridLayout(1000, {160, 260, 20, 10}).origin(0).x == doctest::Approx(60));  // centred: 100 left over

    // Rows are 280 apart (260 + 20); the content starts at 10.
    auto range = grid.visible(0, 600, 100);
    CHECK(range.first == 0);
    CHECK(range.last == 15);  // rows 0..2 (row 2 starts at 570)
    range = grid.visible(280, 260, 100);  // exactly row 1
    CHECK(range.first == 5);
    CHECK(range.last == 10);
    range = grid.visible(281, 300, 100, 1);  // row 1, the top 11 px of row 2 (it starts at 570), plus one extra row
    CHECK(range.first == 5);
    CHECK(range.last == 20);
    CHECK(grid.visible(5000, 600, 12).first == 12);
    CHECK(grid.visible(5000, 600, 12).last == 12);
    CHECK(grid.visible(0, 600, 0).last == 0);

    CHECK_FALSE(grid.neighbour(0, GridMove::Up, 12).has_value());
    CHECK_FALSE(grid.neighbour(5, GridMove::Left, 12).has_value());
    CHECK_FALSE(grid.neighbour(4, GridMove::Right, 12).has_value());
    CHECK_FALSE(grid.neighbour(11, GridMove::Right, 12).has_value());
    CHECK(grid.neighbour(1, GridMove::Right, 12) == 2u);
    CHECK(grid.neighbour(7, GridMove::Up, 12) == 2u);
    CHECK(grid.neighbour(2, GridMove::Down, 12) == 7u);
    CHECK(grid.neighbour(8, GridMove::Down, 12) == 11u);  // the last row is short: its last cell
    CHECK_FALSE(grid.neighbour(11, GridMove::Down, 12).has_value());
    CHECK_FALSE(grid.neighbour(12, GridMove::Down, 12).has_value());
}

}  // TEST_SUITE
