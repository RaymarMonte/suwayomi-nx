// WP-14 (D56): the browse models (src/ui/browse/), the library's add / remove updates and the details screen's "ask the
// source first", on a FakeHttpClient. The fake server below answers like the test server did (SUWAYOMI-API.md).
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "ApiTestKit.hpp"
#include "api/SuwayomiClient.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/browse/LibraryAdder.hpp"
#include "ui/browse/SourceBrowseModel.hpp"
#include "ui/browse/SourceFiltersModel.hpp"
#include "ui/browse/SourcesModel.hpp"
#include "ui/library/LibraryModel.hpp"
#include "ui/manga/MangaModel.hpp"

using namespace snx;
using net::FakeHttpClient;
using net::Request;
using ui::LibraryAdder;
using ui::SourceBrowseModel;
using ui::SourceFiltersModel;
using ui::SourcesModel;

namespace {

const char* kFilters = R"([
  {"__typename":"HeaderFilter","name":"Filters also work without a search text"},
  {"__typename":"TextFilter","name":"Author","textDefault":""},
  {"__typename":"SelectFilter","name":"Status","values":["Any","Ongoing","Completed","On hiatus"],"selectDefault":0},
  {"__typename":"SeparatorFilter","name":""},
  {"__typename":"CheckBoxFilter","name":"In colour","checkDefault":false},
  {"__typename":"GroupFilter","name":"Genres","filters":[
    {"__typename":"TriStateFilter","name":"Action","triDefault":"IGNORE"},
    {"__typename":"TriStateFilter","name":"Comedy","triDefault":"IGNORE"}]},
  {"__typename":"GroupFilter","name":"Type","filters":[
    {"__typename":"CheckBoxFilter","name":"Manga","checkDefault":false},
    {"__typename":"CheckBoxFilter","name":"Webtoon","checkDefault":false}]},
  {"__typename":"SortFilter","name":"Sort by","values":["Popularity","Title","Latest chapter"],
   "sortDefault":{"index":0,"ascending":false}}])";

struct FakeServer {
    std::vector<nlohmann::json> sources = {
        {{"id", "0"}, {"name", "Local source"}, {"displayName", "Local source"}, {"lang", "localsourcelang"}},
        {{"id", "12"}, {"name", "SNX Test"}, {"displayName", "SNX Test (EN)"}, {"lang", "en"}, {"supportsLatest", true}},
        {{"id", "11"}, {"name", "SNX Test"}, {"displayName", "SNX Test (JA)"}, {"lang", "ja"}, {"supportsLatest", true}}};
    int catalogSize = 60;     // manga ids 1000.. in the source
    int pageSize = 25;
    bool ignoresPage = false;  // like Local: every page is all of it, hasNextPage false
    bool repeatsPages = false; // hasNextPage true, but every page is page 1 again
    int failPage = 0;          // that page answers HTTP 500 (once)
    bool failAll = false;
    std::string duplicateTitle;  // LibraryDuplicates finds manga 99 for this title
    bool ownCategories = true;   // Reading (1) and Long series (2) besides Default
    bool longSeriesDefault = false;
    int chapters = 3;            // the Chapters answer for any manga
    bool inLibrary = false;      // the Manga answer
    bool initialized = true;     // the Manga answer
    std::set<int> mangaCategories;  // AddToLibrary's last set
    std::map<int, std::vector<int>> shelves = {{0, {1, 2}}, {1, {3, 4, 5}}};  // LibraryModel: category -> manga ids

    static net::HttpResult data(nlohmann::json value) {
        return FakeHttpClient::answer(200, nlohmann::json{{"data", std::move(value)}}.dump());
    }

    nlohmann::json categoryNodes() const {
        nlohmann::json nodes = nlohmann::json::array({{{"id", 0}, {"name", "Default"}, {"order", 0}, {"default", true}}});
        if (ownCategories) {
            nodes.push_back({{"id", 1}, {"name", "Reading"}, {"order", 1}, {"default", false}});
            nodes.push_back({{"id", 2}, {"name", "Long series"}, {"order", 2}, {"default", longSeriesDefault}});
        }
        return nodes;
    }

    net::HttpResult answer(const Request& request) {
        const std::string operation = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (failAll) {
            return FakeHttpClient::answer(500, "oops");
        }
        if (operation == "Sources") {
            return data({{"sources", {{"nodes", sources}}}});
        }
        if (operation == "SourceFilters") {
            return data({{"source", {{"id", vars["id"]}, {"filters", nlohmann::json::parse(kFilters)}}}});
        }
        if (operation == "BrowseSource") {
            const int page = vars["page"].get<int>();
            if (page == failPage) {
                failPage = 0;
                return FakeHttpClient::answer(500, "oops");
            }
            const int shownPage = repeatsPages ? 1 : page;
            const int from = ignoresPage ? 0 : (shownPage - 1) * pageSize;
            const int to = ignoresPage ? catalogSize : std::min(catalogSize, from + pageSize);
            nlohmann::json manga = nlohmann::json::array();
            for (int i = from; i < to; ++i) {
                manga.push_back({{"id", 1000 + i}, {"title", "Manga " + std::to_string(i)}, {"inLibrary", false},
                                 {"initialized", false}});
            }
            const bool more = ignoresPage ? false : repeatsPages ? true : to < catalogSize;
            return data({{"fetchSourceManga", {{"mangas", manga}, {"hasNextPage", more}}}});
        }
        if (operation == "LibraryDuplicates") {
            nlohmann::json nodes = nlohmann::json::array();
            if (!duplicateTitle.empty() && vars["title"] == duplicateTitle) {
                nodes.push_back({{"id", 99}, {"title", duplicateTitle},
                                 {"source", {{"id", "11"}, {"name", "SNX Test"}, {"displayName", "SNX Test (JA)"}}}});
            }
            return data({{"mangas", {{"nodes", nodes}}}});
        }
        if (operation == "Categories") {
            return data({{"categories", {{"nodes", categoryNodes()}}}});
        }
        if (operation == "FetchMangaAndChapters") {
            initialized = true;
            return data({{"fetchMangaAndChapters", {{"manga", {{"id", vars["id"]}, {"title", "Fetched"}, {"author", "A. Author"},
                                                               {"initialized", true}, {"inLibrary", inLibrary}}},
                                                    {"chapters", nlohmann::json::array()}}}});
        }
        if (operation == "AddToLibrary") {
            mangaCategories.clear();
            nlohmann::json nodes = nlohmann::json::array();
            for (const auto& id : vars["categories"]) {
                mangaCategories.insert(id.get<int>());
                nodes.push_back({{"id", id}, {"name", id.get<int>() == 1 ? "Reading" : "Long series"}});
            }
            inLibrary = true;
            return data({{"added", {{"manga", {{"id", vars["id"]}, {"inLibrary", true}, {"inLibraryAt", 1763370000}}}}},
                         {"categories", {{"manga", {{"id", vars["id"]}, {"categories", {{"nodes", nodes}}}}}}}});
        }
        if (operation == "RemoveFromLibrary") {
            inLibrary = false;
            return data({{"updateManga", {{"manga", {{"id", vars["id"]}, {"inLibrary", false}}}}}});
        }
        if (operation == "Manga") {
            return data({{"manga", {{"id", vars["id"]}, {"title", "Details"}, {"initialized", initialized},
                                    {"inLibrary", inLibrary}}}});
        }
        if (operation == "Chapters") {
            nlohmann::json nodes = nlohmann::json::array();
            for (int i = 0; i < chapters; ++i) {
                nodes.push_back({{"id", 500 + i}, {"name", "Chapter " + std::to_string(i + 1)}, {"sourceOrder", i + 1}});
            }
            return data({{"chapters", {{"nodes", nodes}, {"totalCount", chapters}, {"pageInfo", {{"hasNextPage", false}}}}}});
        }
        if (operation == "LibraryByCategory") {
            const nlohmann::json& filter = vars["filter"]["categoryId"];
            const int id = filter.contains("isNull") ? 0 : filter["equalTo"].get<int>();
            nlohmann::json nodes = nlohmann::json::array();
            for (int manga : shelves[id]) {
                nodes.push_back({{"id", manga}, {"title", "Title " + std::to_string(manga)}});
            }
            return data({{"mangas", {{"nodes", nodes}, {"totalCount", nodes.size()}, {"pageInfo", {{"hasNextPage", false}}}}}});
        }
        return FakeHttpClient::answer(404);
    }
};

struct Harness {
    FakeServer server;
    FakeHttpClient http;
    std::unique_ptr<api::SuwayomiClient> client;

    Harness() {
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = "http://192.168.1.10:4567";
        client = std::make_unique<api::SuwayomiClient>(http, settings, api::ClientOptions());
    }

    std::vector<Request> sent(const std::string& operation) const {
        std::vector<Request> out;
        for (const Request& r : http.requests()) {
            if (operationOf(r) == operation) out.push_back(r);
        }
        return out;
    }
    int asked(const std::string& operation) const { return static_cast<int>(sent(operation).size()); }
    std::vector<std::string> order() const {
        std::vector<std::string> out;
        for (const Request& r : http.requests()) out.push_back(operationOf(r));
        return out;
    }
};

core::Source snxEn(bool supportsLatest = true) {
    core::Source s;
    s.id = "12";
    s.name = "SNX Test";
    s.displayName = "SNX Test (EN)";
    s.lang = "en";
    s.supportsLatest = supportsLatest;
    return s;
}

core::Manga bare(int id, std::string title, bool initialized = false) {
    core::Manga m;
    m.id = id;
    m.title = std::move(title);
    m.initialized = initialized;
    return m;
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("browse sources: rows by language, Last used, hidden languages, only Local, Failed + refresh") {
    Harness h;
    SourcesModel model(*h.client, "", {});
    int changes = 0;
    model.setListener([&] { ++changes; });
    model.start();
    REQUIRE(model.state() == SourcesModel::State::Ready);
    REQUIRE(model.rows().size() == 3);
    CHECK(model.sourceAt(0)->id == "0");
    CHECK(model.rows()[0].label == "Local");
    CHECK(model.rows()[1].label == "English");
    CHECK(model.rows()[2].label == "Japanese");
    CHECK(model.sourceAt(3) == nullptr);
    CHECK(model.languages() == std::vector<std::string>{"localsourcelang", "en", "ja"});
    CHECK(changes > 0);

    model.rememberFocus(2);  // SNX Test (JA)
    model.opened("11");
    REQUIRE(model.rows().size() == 4);
    CHECK(model.rows()[0].lastUsed);
    CHECK(model.rows()[0].label == "Last used");
    CHECK(model.lastSourceId() == "11");
    CHECK(model.rememberedFocus() == 3);  // the same row (not the Last used one)

    model.setHiddenLanguages({"ja"});
    CHECK(model.rows().size() == 2);  // its Last used row goes too
    CHECK(model.hiddenLanguages() == std::vector<std::string>{"ja"});

    h.server.sources.resize(1);  // only Local
    model.refresh();
    CHECK(model.state() == SourcesModel::State::OnlyLocal);

    h.server.failAll = true;
    model.refresh();
    CHECK(model.state() == SourcesModel::State::Failed);
    CHECK(model.rows().empty());
    h.server.failAll = false;
    model.refresh();
    CHECK(model.state() == SourcesModel::State::OnlyLocal);
}

TEST_CASE("browse sources: destroyed while loading") {
    Harness h;
    h.http.hold(true);
    {
        SourcesModel model(*h.client, "", {});
        model.start();
        CHECK(model.state() == SourcesModel::State::Loading);
    }
    h.http.releaseAll();  // the answer finds the model gone: nothing happens
}

TEST_CASE("browse a source: popular pages 25 + 25 + 10 as the grid scrolls, then no more") {
    Harness h;
    SourceBrowseModel model(*h.client, snxEn(), 1000);
    model.start();
    REQUIRE(model.state() == SourceBrowseModel::State::Ready);
    CHECK(model.items().size() == 25);
    CHECK(model.hasNext());
    CHECK(model.mode() == SourceBrowseModel::Mode::Popular);
    CHECK(variablesOf(h.sent("BrowseSource").at(0))["page"] == 1);
    CHECK(variablesOf(h.sent("BrowseSource").at(0))["type"] == "POPULAR");

    model.showing(5);  // far from the end: nothing
    CHECK(h.asked("BrowseSource") == 1);
    model.showing(13);  // the last half page
    CHECK(model.items().size() == 50);
    CHECK(variablesOf(h.sent("BrowseSource").at(1))["page"] == 2);
    model.showing(49);
    CHECK(model.items().size() == 60);
    CHECK_FALSE(model.hasNext());
    CHECK(model.pagesLoaded() == 3);
    model.showing(59);
    CHECK(h.asked("BrowseSource") == 3);
}

TEST_CASE("browse a source: a page with nothing new ends the list (Local); the limit; a manga seen twice shows once") {
    Harness h;
    h.server.repeatsPages = true;  // hasNextPage true, but page 2 is page 1 again
    SourceBrowseModel model(*h.client, snxEn(), 1000);
    model.start();
    model.showing(24);
    CHECK(model.items().size() == 25);
    CHECK_FALSE(model.hasNext());

    Harness local;
    local.server.ignoresPage = true;
    local.server.catalogSize = 157;
    SourceBrowseModel all(*local.client, snxEn(), 1000);
    all.start();
    CHECK(all.items().size() == 157);
    CHECK_FALSE(all.hasNext());

    Harness capped;
    SourceBrowseModel limited(*capped.client, snxEn(), 30);
    limited.start();
    limited.showing(24);
    CHECK(limited.items().size() == 30);
    CHECK_FALSE(limited.hasNext());
}

TEST_CASE("browse a source: Failed + retry; a later page fails, keeps what it has, retries") {
    Harness h;
    h.server.failPage = 1;
    SourceBrowseModel model(*h.client, snxEn(), 1000);
    model.start();
    CHECK(model.state() == SourceBrowseModel::State::Failed);
    model.retry();
    CHECK(model.state() == SourceBrowseModel::State::Ready);

    h.server.failPage = 2;
    model.showing(20);
    CHECK(model.moreFailed());
    CHECK(model.items().size() == 25);
    CHECK(model.state() == SourceBrowseModel::State::Ready);
    model.showing(24);  // no automatic retry
    CHECK(h.asked("BrowseSource") == 3);
    model.retry();
    CHECK_FALSE(model.moreFailed());
    CHECK(model.items().size() == 50);
}

TEST_CASE("browse a source: search with a text and filters, clear, Latest, the In library tag, destroyed while loading") {
    Harness h;
    SourceBrowseModel model(*h.client, snxEn(), 1000);
    model.start();
    const auto list = model.listRevision();
    const nlohmann::json changes = nlohmann::json::parse(R"([{"position":2,"selectState":2}])");
    model.search("comet", changes, 1);
    CHECK(model.mode() == SourceBrowseModel::Mode::Search);
    CHECK(model.query() == "comet");
    CHECK(model.filtersChanged() == 1);
    CHECK(model.listRevision() > list);
    const nlohmann::json vars = variablesOf(h.sent("BrowseSource").back());
    CHECK(vars["type"] == "SEARCH");
    CHECK(vars["query"] == "comet");
    CHECK(vars["filters"] == changes);

    model.search("", nlohmann::json::array(), 0);  // nothing to search: popular
    CHECK(model.mode() == SourceBrowseModel::Mode::Popular);
    model.search("x", nlohmann::json::array(), 0);
    model.clearSearch();
    CHECK(model.mode() == SourceBrowseModel::Mode::Popular);
    CHECK(model.query().empty());
    CHECK(variablesOf(h.sent("BrowseSource").back())["type"] == "POPULAR");

    model.setMode(SourceBrowseModel::Mode::Latest);
    CHECK(variablesOf(h.sent("BrowseSource").back())["type"] == "LATEST");
    SourceBrowseModel noLatest(*h.client, snxEn(false), 1000);
    noLatest.start();
    noLatest.setMode(SourceBrowseModel::Mode::Latest);
    CHECK(noLatest.mode() == SourceBrowseModel::Mode::Popular);

    const auto items = model.itemsRevision();
    model.setInLibrary(model.items()[3].id, true);
    CHECK(model.items()[3].inLibrary);
    CHECK(model.itemsRevision() == items + 1);
    model.setInLibrary(424242, true);  // not listed
    CHECK(model.itemsRevision() == items + 1);
    CHECK(ui::browseModeName(SourceBrowseModel::Mode::Latest) == "latest");

    h.http.hold(true);
    {
        SourceBrowseModel gone(*h.client, snxEn(), 1000);
        gone.start();
        CHECK(gone.state() == SourceBrowseModel::State::Loading);
    }
    h.http.releaseAll();
}

TEST_CASE("browse filters: rows with groups, edits, apply, leave without applying, reset, Failed + retry") {
    Harness h;
    h.server.failAll = true;
    SourceFiltersModel model(*h.client, "12");
    model.start();
    CHECK(model.state() == SourceFiltersModel::State::Failed);
    h.server.failAll = false;
    model.retry();
    REQUIRE(model.state() == SourceFiltersModel::State::Ready);
    model.beginEdit();
    // Header, Author, Status, In colour, Genres, Type, Sort by (the separator isn't a row).
    REQUIRE(model.rows().size() == 7);
    CHECK(model.filterAt(4)->name == "Genres");
    CHECK(model.valueAt(4) == nullptr);  // a group heading has no value
    model.toggleGroup(4);
    REQUIRE(model.rows().size() == 9);
    CHECK(model.rows()[5].depth == 1);
    CHECK(model.filterAt(5)->name == "Action");

    model.setSelect(2, 2);          // Status = Completed
    model.cycleTri(5);              // Action: include
    model.toggleCheck(3);           // In colour
    model.chooseSort(8, 1);         // Title, descending
    model.chooseSort(8, 1);         // the same again: ascending
    model.setText(1, "Aiko");
    model.toggleCheck(1);           // the wrong kind: nothing
    model.setSelect(2, 9);          // no such value: nothing
    CHECK(model.valueAt(2)->select == 2);
    CHECK(model.valueAt(8)->sort->ascending);
    CHECK(model.editCount() == 5);
    CHECK(model.groupChangedCount(5) == 1);
    CHECK(model.appliedCount() == 0);

    model.apply();
    CHECK(model.appliedCount() == 5);
    CHECK(model.appliedChanges().size() == 5);

    model.beginEdit();  // a new visit: the applied values, groups closed
    CHECK(model.rows().size() == 7);
    model.toggleCheck(3);
    CHECK(model.editCount() == 4);
    model.beginEdit();  // left without Apply: nothing changed
    CHECK(model.editCount() == 5);
    model.reset();
    CHECK(model.editCount() == 0);
    CHECK(model.appliedCount() == 5);
    model.apply();
    CHECK(model.appliedChanges() == nlohmann::json::array());
}

TEST_CASE("add to library: asks for categories with the default-marked ones ticked, adds exactly the chosen ones") {
    Harness h;
    h.server.longSeriesDefault = true;
    LibraryAdder adder(*h.client);
    REQUIRE(adder.add(bare(1000, "Comet Courier", true), true));
    REQUIRE(adder.step() == LibraryAdder::Step::AskCategories);
    CHECK(adder.preticked() == std::set<int>{2});
    CHECK(adder.asked());
    CHECK_FALSE(adder.add(bare(1001, "Other"), true));  // busy
    adder.chooseCategories({1, 0});
    REQUIRE(adder.step() == LibraryAdder::Step::Done);
    CHECK(adder.result().inLibrary);
    CHECK(adder.result().categories.size() == 1);
    CHECK(variablesOf(h.sent("AddToLibrary").at(0))["categories"] == nlohmann::json::array({1}));
    CHECK(h.asked("FetchMangaAndChapters") == 0);  // it has its details
    CHECK(ui::adderStepName(adder.step()) == "done");
}

TEST_CASE("add to library: without asking, the default-marked ones or none; no own categories; details first") {
    Harness h;
    h.server.longSeriesDefault = true;
    LibraryAdder adder(*h.client);
    adder.add(bare(1000, "A", true), false);  // Don't ask again
    REQUIRE(adder.step() == LibraryAdder::Step::Done);
    CHECK_FALSE(adder.asked());
    CHECK(h.server.mangaCategories == std::set<int>{2});

    h.server.longSeriesDefault = false;
    adder.add(bare(1001, "B", true), false);
    CHECK(h.server.mangaCategories.empty());  // none = Default

    h.server.ownCategories = false;
    adder.add(bare(1002, "C"), true);  // asking is on, but only Default exists: no question
    REQUIRE(adder.step() == LibraryAdder::Step::Done);
    CHECK_FALSE(adder.asked());
    // A bare row from a grid: its details and chapters first, then the add.
    const auto order = h.order();
    REQUIRE(order.size() >= 2);
    CHECK(order[order.size() - 2] == "FetchMangaAndChapters");
    CHECK(order.back() == "AddToLibrary");
    CHECK(adder.result().title == "Fetched");
    CHECK(adder.result().initialized);
}

TEST_CASE("add to library: the same title asks first; Add anyway goes on; Cancel adds nothing; failures") {
    Harness h;
    h.server.duplicateTitle = "Paper Garden";
    LibraryAdder adder(*h.client);
    int changes = 0;
    adder.setListener([&] { ++changes; });
    adder.add(bare(1013, "Paper Garden", true), true);
    REQUIRE(adder.step() == LibraryAdder::Step::AskDuplicate);
    REQUIRE(adder.duplicate() != nullptr);
    CHECK(adder.duplicate()->id == 99);
    CHECK(adder.duplicate()->source->displayName == "SNX Test (JA)");
    CHECK(variablesOf(h.sent("LibraryDuplicates").at(0))["title"] == "Paper Garden");
    adder.cancel();
    CHECK(adder.step() == LibraryAdder::Step::Idle);
    CHECK(h.asked("AddToLibrary") == 0);

    adder.add(bare(1013, "Paper Garden", true), true);
    adder.addAnyway();
    CHECK(adder.step() == LibraryAdder::Step::AskCategories);
    CHECK(adder.sawDuplicate());
    adder.chooseCategories({});
    CHECK(adder.step() == LibraryAdder::Step::Done);
    CHECK(changes > 0);

    h.server.failAll = true;
    adder.add(bare(1014, "X", true), true);
    CHECK(adder.step() == LibraryAdder::Step::Failed);
    CHECK(adder.error().kind == api::ApiErrorKind::Server);
}

TEST_CASE("remove from library; destroyed while adding") {
    Harness h;
    LibraryAdder adder(*h.client);
    core::Manga manga = bare(1000, "Comet Courier", true);
    manga.inLibrary = true;
    REQUIRE(adder.removeFromLibrary(manga));
    REQUIRE(adder.step() == LibraryAdder::Step::Done);
    CHECK(adder.removing());
    CHECK_FALSE(adder.result().inLibrary);
    CHECK(h.asked("RemoveFromLibrary") == 1);

    h.http.hold(true);
    {
        LibraryAdder gone(*h.client);
        gone.add(bare(1000, "A", true), true);
        CHECK(gone.step() == LibraryAdder::Step::Checking);
    }
    h.http.releaseAll();
}

TEST_CASE("library: a manga added joins its shelves; removed leaves every shelf, the focus keeps its index") {
    Harness h;
    ui::LibraryModel library(*h.client, core::LibrarySettings{1, core::LibrarySort::Title}, 60);
    library.start();
    REQUIRE(library.state() == ui::LibraryModel::State::Ready);
    REQUIRE(library.category()->id == 1);
    CHECK(library.items().size() == 3);

    // Added to Reading (on screen): it loads again.
    h.server.shelves[1].push_back(1000);
    core::Manga added = bare(1000, "Comet Courier", true);
    added.inLibrary = true;
    core::Category reading;
    reading.id = 1;
    added.categories = {reading};
    library.rememberFocus(2);
    library.mangaAdded(added);
    CHECK(library.items().size() == 4);
    CHECK(library.rememberedFocus() == 2);

    // Removed: it leaves the shelf on screen without a request.
    const int before = h.asked("LibraryByCategory");
    library.mangaRemoved(4);
    CHECK(library.items().size() == 3);
    CHECK(h.asked("LibraryByCategory") == before);
    library.mangaRemoved(424242);  // not in the library: nothing
    CHECK(library.items().size() == 3);
}

TEST_CASE("details: a bare manga asks its source first; a failure there retries the source; an empty one outside") {
    Harness h;
    h.http.hold(true);
    ui::MangaModel model(*h.client, bare(1000, "Comet Courier"), 10000);
    model.start();
    CHECK(model.loadingFromSource());
    CHECK(model.state() == ui::MangaModel::State::Loading);
    CHECK(model.manga().title == "Comet Courier");  // the title shows meanwhile
    h.http.hold(false);
    h.http.releaseAll();
    CHECK_FALSE(model.loadingFromSource());
    CHECK(model.state() == ui::MangaModel::State::Ready);
    CHECK(h.order().front() == "FetchMangaAndChapters");
    CHECK(h.asked("FetchMangaAndChapters") == 1);

    Harness f;
    f.server.failAll = true;
    ui::MangaModel failing(*f.client, bare(1000, "X"), 10000);
    failing.start();
    CHECK(failing.state() == ui::MangaModel::State::Failed);
    f.server.failAll = false;
    failing.retry();
    CHECK(failing.state() == ui::MangaModel::State::Ready);
    CHECK(f.asked("FetchMangaAndChapters") == 2);

    // Initialized but no chapters and not in the library (Mihon's rule): the source once.
    Harness e;
    e.server.chapters = 0;
    ui::MangaModel empty(*e.client, bare(1000, "Coming Soon Saga", true), 10000);
    empty.start();
    CHECK(empty.state() == ui::MangaModel::State::Empty);
    CHECK(e.asked("FetchMangaAndChapters") == 1);
    CHECK(e.asked("Chapters") == 2);

    // In the library with no chapters: no automatic source call.
    Harness l;
    l.server.chapters = 0;
    l.server.inLibrary = true;
    core::Manga inLibrary = bare(1, "Library manga", true);
    inLibrary.inLibrary = true;
    ui::MangaModel own(*l.client, inLibrary, 10000);
    own.start();
    CHECK(own.state() == ui::MangaModel::State::Empty);
    CHECK(l.asked("FetchMangaAndChapters") == 0);

    own.setInLibrary(false, {});
    CHECK_FALSE(own.manga().inLibrary);
}

}  // TEST_SUITE
