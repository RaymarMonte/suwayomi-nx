// WP-24 (D54): the Categories screen's model and the category picker's model (src/ui/categories/) on a FakeHttpClient.
// The fake server below keeps categories and one manga's memberships in memory and answers like the test server did.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#include "ApiTestKit.hpp"
#include "api/SuwayomiClient.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/categories/CategoriesModel.hpp"
#include "ui/categories/CategoryPickerModel.hpp"

using namespace snx;
using net::FakeHttpClient;
using net::Request;
using ui::CategoriesModel;
using ui::CategoryPickerModel;

namespace {

struct FakeCategory {
    int id;
    std::string name;
    int count;
};

struct FakeServer {
    std::vector<FakeCategory> categories{{0, "Default", 2}, {1, "Reading", 3}, {2, "Long series", 2}, {4, "Empty", 0}};
    std::set<int> mangaCategories{1};  // manga 1's
    int nextId = 33;
    bool fail = false;  // every answer is an HTTP 500

    nlohmann::json node(const FakeCategory& c, std::size_t order, bool withCount) const {
        nlohmann::json n = {{"id", c.id}, {"name", c.name}, {"order", order}, {"default", c.id == 0}};
        if (withCount) {
            n["mangas"] = {{"totalCount", c.count}};
        }
        return n;
    }
    nlohmann::json list(bool withCount) const {
        nlohmann::json nodes = nlohmann::json::array();
        for (std::size_t i = 0; i < categories.size(); ++i) {
            nodes.push_back(node(categories[i], i, withCount));
        }
        return nodes;
    }
    nlohmann::json mangaNode() const {
        nlohmann::json nodes = nlohmann::json::array();
        for (const FakeCategory& c : categories) {
            if (mangaCategories.count(c.id) > 0) {
                nodes.push_back({{"id", c.id}, {"name", c.name}});
            }
        }
        return {{"id", 1}, {"title", "Alpha"}, {"categories", {{"nodes", nodes}}}};
    }
    static net::HttpResult data(nlohmann::json value) {
        return FakeHttpClient::answer(200, nlohmann::json{{"data", std::move(value)}}.dump());
    }

    net::HttpResult answer(const Request& request) {
        if (fail) {
            return FakeHttpClient::answer(500, "oops");
        }
        const std::string operation = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (operation == "ManageCategories" || operation == "Categories") {
            return data({{"categories", {{"nodes", list(operation == "ManageCategories")}}}});
        }
        if (operation == "Manga") {
            return data({{"manga", mangaNode()}});
        }
        if (operation == "CreateCategory") {
            categories.push_back({nextId++, vars["name"].get<std::string>(), 0});
            return data({{"createCategory", {{"category", node(categories.back(), categories.size() - 1, false)}}}});
        }
        if (operation == "UpdateCategory") {
            for (std::size_t i = 0; i < categories.size(); ++i) {
                if (categories[i].id == vars["id"].get<int>()) {
                    categories[i].name = vars["name"].get<std::string>();
                    return data({{"updateCategory", {{"category", node(categories[i], i, false)}}}});
                }
            }
        }
        if (operation == "UpdateCategoryOrder") {
            const int id = vars["id"].get<int>();
            const auto found = std::find_if(categories.begin(), categories.end(),
                                            [id](const FakeCategory& c) { return c.id == id; });
            const FakeCategory moved = *found;
            categories.erase(found);
            const std::size_t at = std::min<std::size_t>(vars["position"].get<std::size_t>(), categories.size());
            categories.insert(categories.begin() + static_cast<long>(at), moved);
            return data({{"updateCategoryOrder", {{"categories", list(false)}}}});
        }
        if (operation == "DeleteCategory") {
            const int id = vars["id"].get<int>();
            categories.erase(std::remove_if(categories.begin(), categories.end(),
                                            [id](const FakeCategory& c) { return c.id == id; }),
                             categories.end());
            return data({{"deleteCategory", {{"category", {{"id", id}}}, {"mangas", nlohmann::json::array()}}}});
        }
        if (operation == "UpdateMangaCategories") {
            for (const auto& id : vars["add"]) {
                mangaCategories.insert(id.get<int>());
            }
            for (const auto& id : vars["remove"]) {
                mangaCategories.erase(id.get<int>());
            }
            return data({{"updateMangaCategories", {{"manga", mangaNode()}}}});
        }
        return FakeHttpClient::answer(404);
    }
};

struct Harness {
    FakeServer server;
    FakeHttpClient http;
    std::unique_ptr<api::SuwayomiClient> client;
    int changes = 0;

    Harness() {
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = "http://192.168.1.10:4567";
        client = std::make_unique<api::SuwayomiClient>(http, settings, api::ClientOptions());
    }

    int asked(const std::string& operation) const {
        int n = 0;
        for (const Request& r : http.requests()) {
            n += operationOf(r) == operation ? 1 : 0;
        }
        return n;
    }
};

std::vector<std::string> names(const std::vector<core::Category>& list) {
    std::vector<std::string> out;
    for (const auto& c : list) {
        out.push_back(c.name);
    }
    return out;
}

std::vector<std::string> checkedNames(const CategoryPickerModel& model) {
    std::vector<std::string> out;
    for (const auto& row : model.rows()) {
        if (row.checked) {
            out.push_back(row.category.name);
        }
    }
    return out;
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("categories: the list without Default, with counts; Failed and retry; only Default = Empty") {
    Harness h;
    CategoriesModel model(*h.client, 64);
    model.setListener([&h] { ++h.changes; });
    CHECK(model.state() == CategoriesModel::State::Loading);
    model.start();
    REQUIRE(model.state() == CategoriesModel::State::Ready);
    CHECK(names(model.items()) == std::vector<std::string>{"Reading", "Long series", "Empty"});
    CHECK(model.items()[0].mangaCount == 3);
    CHECK_FALSE(model.changedAnything());
    model.start();  // only once
    CHECK(h.asked("ManageCategories") == 1);

    h.server.fail = true;
    CategoriesModel failing(*h.client, 64);
    failing.start();
    REQUIRE(failing.state() == CategoriesModel::State::Failed);
    CHECK(failing.error().kind == api::ApiErrorKind::Server);
    h.server.fail = false;
    failing.retry();
    CHECK(failing.state() == CategoriesModel::State::Ready);

    h.server.categories = {{0, "Default", 5}};
    CategoriesModel empty(*h.client, 64);
    empty.start();
    CHECK(empty.state() == CategoriesModel::State::Empty);
    CHECK(empty.add("First"));  // Add works from Empty
    CHECK(empty.state() == CategoriesModel::State::Ready);
}

TEST_CASE("categories: add, rename, move, delete; the list is the server's answer; the focus follows") {
    Harness h;
    CategoriesModel model(*h.client, 64);
    model.start();
    const auto revision = model.revision();

    REQUIRE(model.add("  Later "));
    CHECK(names(model.items()) == std::vector<std::string>{"Reading", "Long series", "Empty", "Later"});
    CHECK(model.items()[3].mangaCount == 0);
    CHECK(model.rememberedFocus() == 3);
    CHECK(model.changedAnything());
    CHECK(model.revision() > revision);
    auto done = model.takeDone();
    REQUIRE(done);
    CHECK(done->action == CategoriesModel::Action::Add);
    CHECK(done->ok);
    CHECK(done->categoryId == 33);
    CHECK_FALSE(model.takeDone());

    REQUIRE(model.renameAt(3, "Soon"));
    CHECK(model.items()[3].name == "Soon");
    CHECK(h.server.categories.back().name == "Soon");

    REQUIRE(model.move(3, -1));  // Soon up, above Empty
    CHECK(names(model.items()) == std::vector<std::string>{"Reading", "Long series", "Soon", "Empty"});
    CHECK(model.rememberedFocus() == 2);
    CHECK(model.items()[0].mangaCount == 3);  // the counts survive the order answer (it has none)
    const auto moveSent = variablesOf(h.http.requests().back());
    CHECK(moveSent["position"] == 3);
    CHECK_FALSE(model.move(0, -1));  // Reading is first: nothing sent
    CHECK_FALSE(model.move(3, 1));   // Empty is last

    model.rememberFocus(2);
    REQUIRE(model.deleteAt(2));  // Soon
    CHECK(names(model.items()) == std::vector<std::string>{"Reading", "Long series", "Empty"});
    CHECK(model.rememberedFocus() == 2);  // the next one, Empty
    done = model.takeDone();
    REQUIRE(done);
    CHECK(done->action == CategoriesModel::Action::Delete);
}

TEST_CASE("categories: names are checked before anything is sent") {
    Harness h;
    CategoriesModel model(*h.client, 10);
    model.start();
    const auto before = h.http.requests().size();
    CHECK(model.checkName("  ") == core::NameProblem::Empty);
    CHECK_FALSE(model.add("  "));
    CHECK(model.checkName("reading") == core::NameProblem::Taken);
    CHECK_FALSE(model.add("reading"));
    CHECK(model.checkName("default") == core::NameProblem::Taken);
    CHECK(model.checkName("A very long name") == core::NameProblem::TooLong);
    CHECK_FALSE(model.renameAt(1, "Reading"));  // Long series -> Reading: taken
    CHECK_FALSE(model.renameAt(0, "Reading"));  // the same name: nothing to do
    CHECK(model.checkName("READING", 0) == core::NameProblem::None);  // its own name in other case is fine
    CHECK_FALSE(model.renameAt(9, "X"));        // no such row
    CHECK(h.http.requests().size() == before);
}

TEST_CASE("categories: one action at a time; a failed action keeps the list and says why; destroyed while busy") {
    Harness h;
    auto model = std::make_unique<CategoriesModel>(*h.client, 64);
    model->start();
    h.http.hold(true);
    REQUIRE(model->add("Later"));
    CHECK(model->busy());
    CHECK_FALSE(model->renameAt(0, "Other"));
    CHECK_FALSE(model->deleteAt(0));
    h.http.hold(false);
    h.http.releaseAll();
    CHECK_FALSE(model->busy());

    h.server.fail = true;
    REQUIRE(model->deleteAt(0));
    CHECK_FALSE(model->busy());
    const auto error = model->takeActionError();
    REQUIRE(error);
    CHECK(error->kind == api::ApiErrorKind::Server);
    CHECK(names(model->items()) == std::vector<std::string>{"Reading", "Long series", "Empty", "Later"});
    const auto done = model->takeDone();
    REQUIRE(done);
    CHECK_FALSE(done->ok);

    h.server.fail = false;
    h.http.hold(true);
    REQUIRE(model->deleteAt(0));
    model.reset();  // the screen closes while the delete is on its way
    h.http.hold(false);
    h.http.releaseAll();  // the cancelled request finds no model: nothing happens (no crash, no listener call)
    CHECK(h.server.categories.size() == 5);  // the fake drops a cancelled request before it is sent
}

TEST_CASE("picker: the manga's categories ticked; save sends only the difference; nothing changed = no request") {
    Harness h;
    h.server.mangaCategories = {1, 4};
    CategoryPickerModel model(*h.client, 1, std::nullopt, 64);
    model.setListener([&h] { ++h.changes; });
    model.start();
    REQUIRE(model.state() == CategoryPickerModel::State::Ready);
    CHECK(model.rows().size() == 3);  // no Default
    CHECK(checkedNames(model) == std::vector<std::string>{"Reading", "Empty"});
    CHECK(h.asked("Manga") == 1);
    CHECK_FALSE(model.changed());

    SUBCASE("nothing changed") {
        REQUIRE(model.save());
        CHECK(model.state() == CategoryPickerModel::State::Saved);
        CHECK_FALSE(model.savedChange());
        CHECK(names(model.saved()) == std::vector<std::string>{"Reading", "Empty"});
        CHECK(h.asked("UpdateMangaCategories") == 0);
    }
    SUBCASE("tick Long series, untick Empty") {
        model.toggle(1);
        model.toggle(2);
        CHECK(model.changed());
        CHECK(model.chosen() == std::set<int>{1, 2});
        REQUIRE(model.save());
        CHECK(model.state() == CategoryPickerModel::State::Saved);
        CHECK(model.savedChange());
        CHECK(names(model.saved()) == std::vector<std::string>{"Reading", "Long series"});
        const auto vars = variablesOf(h.http.requests().back());
        CHECK(vars["add"] == nlohmann::json::array({2}));
        CHECK(vars["remove"] == nlohmann::json::array({4}));
        CHECK(h.server.mangaCategories == std::set<int>{1, 2});
    }
    SUBCASE("untick all: the manga goes to Default") {
        model.toggle(0);
        model.toggle(2);
        REQUIRE(model.save());
        CHECK(model.saved().empty());
        CHECK(h.server.mangaCategories.empty());
    }
    SUBCASE("a failed save keeps the ticks and can be tried again") {
        model.toggle(1);
        h.server.fail = true;
        REQUIRE(model.save());
        CHECK(model.state() == CategoryPickerModel::State::Ready);
        CHECK(model.takeActionError());
        CHECK(checkedNames(model) == std::vector<std::string>{"Reading", "Long series", "Empty"});
        h.server.fail = false;
        REQUIRE(model.save());
        CHECK(model.state() == CategoryPickerModel::State::Saved);
    }
}

TEST_CASE("picker: given categories (the details screen) skip the manga request; New category comes ticked") {
    Harness h;
    core::Category reading;
    reading.id = 1;
    reading.name = "Reading";
    CategoryPickerModel model(*h.client, 1, std::vector<core::Category>{reading}, 64);
    model.start();
    REQUIRE(model.state() == CategoryPickerModel::State::Ready);
    CHECK(h.asked("Manga") == 0);
    CHECK(checkedNames(model) == std::vector<std::string>{"Reading"});

    CHECK(model.checkName("long SERIES") == core::NameProblem::Taken);
    CHECK_FALSE(model.addCategory("long SERIES"));
    CHECK_FALSE(model.addCategory(""));
    REQUIRE(model.addCategory("Later"));
    REQUIRE(model.rows().size() == 4);
    CHECK(model.rows().back().category.name == "Later");
    CHECK(model.rows().back().checked);
    CHECK(model.checkName("later") == core::NameProblem::Taken);  // the new one counts too
    REQUIRE(model.save());
    CHECK(h.server.mangaCategories == std::set<int>{1, 33});
}

TEST_CASE("picker: a failed load and retry; destroyed while loading") {
    Harness h;
    h.server.fail = true;
    CategoryPickerModel model(*h.client, 1, std::nullopt, 64);
    model.start();
    REQUIRE(model.state() == CategoryPickerModel::State::Failed);
    CHECK_FALSE(model.save());
    model.toggle(0);  // nothing to toggle
    h.server.fail = false;
    model.retry();
    CHECK(model.state() == CategoryPickerModel::State::Ready);

    h.http.hold(true);
    auto gone = std::make_unique<CategoryPickerModel>(*h.client, 1, std::nullopt, 64);
    gone->start();
    CHECK(gone->state() == CategoryPickerModel::State::Loading);
    gone.reset();
    h.http.hold(false);
    h.http.releaseAll();  // the answers find no model
}

}  // TEST_SUITE
