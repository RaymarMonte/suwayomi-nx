// WP-24 (D54): the library tools' rules and their GraphQL requests and answers.
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/LibraryTools.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/graphql/Documents.hpp"
#include "core/graphql/Gql.hpp"
#include "core/graphql/Operations.hpp"

using namespace snx::core;

namespace {

GqlResponse answer(const char* body) {
    auto r = parseGqlResponse(body);
    REQUIRE(r.ok());
    return r.value();
}

Category cat(int id, std::string name, int order) {
    Category c;
    c.id = id;
    c.name = std::move(name);
    c.order = order;
    c.isDefault = id == kDefaultCategoryId;
    return c;
}

// The seed's categories (TESTING.md): Default, Reading, Long series, Big shelf, Empty.
std::vector<Category> seed() {
    return {cat(0, "Default", 0), cat(1, "Reading", 1), cat(2, "Long series", 2), cat(3, "Big shelf", 3),
            cat(4, "Empty", 4)};
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("likeEscape: backslash, % and _ match themselves, everything else stays") {
        CHECK(likeEscape("alpha") == "alpha");
        CHECK(likeEscape("a_p") == "a\\_p");
        CHECK(likeEscape("100%") == "100\\%");
        CHECK(likeEscape("a\\b") == "a\\\\b");
        CHECK(likeEscape("") == "");
        CHECK(likeEscape("日本語") == "日本語");
    }

    TEST_CASE("trimText: ASCII spaces and the ideographic space at both ends, not inside") {
        CHECK(trimText("  Reading \t\n") == "Reading");
        CHECK(trimText("Long series") == "Long series");
        CHECK(trimText("   ") == "");
        CHECK(trimText("") == "");
        CHECK(trimText("\xE3\x80\x80日本\xE3\x80\x80 ") == "日本");
        CHECK(trimText("a\xE3\x80\x80" "b") == "a\xE3\x80\x80" "b");
    }

    TEST_CASE("utf8Length and utf8Cut count characters, never half of one") {
        CHECK(utf8Length("abc") == 3);
        CHECK(utf8Length("日本語") == 3);
        CHECK(utf8Length("") == 0);
        CHECK(utf8Cut("日本語テスト", 2) == "日本");
        CHECK(utf8Cut("abc", 5) == "abc");
        CHECK(utf8Cut("abc", 0) == "");
        CHECK(utf8Cut("a日b", 2) == "a日");
    }

    TEST_CASE("checkCategoryName: empty, too long, taken ignoring case (Default too), own name allowed") {
        const auto categories = seed();
        CHECK(checkCategoryName("Favourites", categories, -1, 64) == NameProblem::None);
        CHECK(checkCategoryName("", categories, -1, 64) == NameProblem::Empty);
        CHECK(checkCategoryName("reading", categories, -1, 64) == NameProblem::Taken);
        CHECK(checkCategoryName("DEFAULT", categories, -1, 64) == NameProblem::Taken);
        CHECK(checkCategoryName("Reading", categories, 1, 64) == NameProblem::None);   // renaming Reading to itself
        CHECK(checkCategoryName("reading", categories, 1, 64) == NameProblem::None);   // ... or its own case
        CHECK(checkCategoryName("Reading", categories, 2, 64) == NameProblem::Taken);  // Long series -> Reading
        CHECK(checkCategoryName(std::string(65, 'x'), categories, -1, 64) == NameProblem::TooLong);
        CHECK(checkCategoryName(std::string(64, 'x'), categories, -1, 64) == NameProblem::None);
        CHECK(checkCategoryName("日本語", categories, -1, 3) == NameProblem::None);  // 3 characters, 9 bytes
        // A name on the server with spaces around it still counts as taken.
        auto spaced = categories;
        spaced.push_back(cat(9, "  Later ", 5));
        CHECK(checkCategoryName("later", spaced, -1, 64) == NameProblem::Taken);
        CHECK(nameProblemName(NameProblem::Taken) == "taken");
        CHECK(nameProblemName(NameProblem::TooLong) == "too_long");
    }

    TEST_CASE("categoryChange: only the difference, sorted, never Default") {
        const auto change = categoryChange({1, 3}, {2, 3, 4});
        CHECK(change.add == std::vector<int>{2, 4});
        CHECK(change.remove == std::vector<int>{1});
        CHECK(categoryChange({1}, {1}).empty());
        CHECK(categoryChange({}, {0}).empty());
        CHECK(categoryChange({0, 1}, {}).remove == std::vector<int>{1});
    }

    TEST_CASE("categoryPosition: the server's 1-based place after Default, 0 when it can't move") {
        const auto categories = seed();
        CHECK(categoryPosition(categories, 2, -1) == 1);  // Long series up: first after Default
        CHECK(categoryPosition(categories, 2, 1) == 3);   // down: after Big shelf
        CHECK(categoryPosition(categories, 1, -1) == 0);  // Reading is already first
        CHECK(categoryPosition(categories, 4, 1) == 0);   // Empty is already last
        CHECK(categoryPosition(categories, 0, 1) == 0);   // Default never moves
        CHECK(categoryPosition(categories, 99, 1) == 0);  // unknown
        CHECK(categoryPosition(categories, 3, 0) == 0);
        // Without Default in the list it is the same place.
        const std::vector<Category> noDefault(categories.begin() + 1, categories.end());
        CHECK(categoryPosition(noDefault, 2, -1) == 1);
    }

    TEST_CASE("RuntimeProfile: the library tools' limits") {
        const RuntimeProfile full = makeRuntimeProfile(LaunchMode::Application, 3ull << 30, 0);
        CHECK(full.searchMaxChars == 64);
        CHECK(full.categoryNameMaxChars == 64);
    }

    TEST_CASE("WP-24: the search request is LibraryByCategory with an escaped title filter") {
        const auto r = gql::librarySearch("a_p", 60, 120);
        CHECK(r.name == "LibraryByCategory");
        CHECK(r.variables["filter"] == nlohmann::json::parse(R"({"title": {"includesInsensitive": "a\\_p"}})"));
        CHECK(r.variables["first"] == 60);
        CHECK(r.variables["offset"] == 120);
        CHECK_FALSE(r.mutation);
        const auto page = gql::readLibrarySearch(answer(R"({"data": {"mangas": {"nodes": [
            {"id": 1, "title": "Alpha", "unreadCount": 2}], "totalCount": 1}}})"));
        REQUIRE(page.ok());
        CHECK(page.value().totalCount == 1);
        CHECK(page.value().nodes.at(0).title == "Alpha");
    }

    TEST_CASE("WP-24: the category documents exist and their requests carry the right variables") {
        for (const char* name : {"ManageCategories", "CreateCategory", "UpdateCategory", "UpdateCategoryOrder",
                                 "DeleteCategory", "UpdateMangaCategories"}) {
            CHECK_MESSAGE(!gql::document(name).text.empty(), name);
        }
        CHECK_FALSE(gql::manageCategories().mutation);
        const auto create = gql::createCategory("Later");
        CHECK(create.mutation);
        CHECK(create.variables["name"] == "Later");
        const auto rename = gql::renameCategory(3, "Huge shelf");
        CHECK(rename.mutation);
        CHECK(rename.variables["id"] == 3);
        CHECK(rename.variables["name"] == "Huge shelf");
        const auto move = gql::moveCategory(2, 1);
        CHECK(move.mutation);
        CHECK(move.variables["position"] == 1);
        CHECK(gql::moveCategory(2, 0).variables["position"] == 1);  // the server refuses 0
        CHECK(gql::deleteCategory(4).variables["id"] == 4);
        const auto set = gql::updateMangaCategories(1, {2, 0}, {1});
        CHECK(set.mutation);
        CHECK(set.variables["id"] == 1);
        CHECK(set.variables["add"] == nlohmann::json::array({2}));  // Default left out
        CHECK(set.variables["remove"] == nlohmann::json::array({1}));
        CHECK(gql::updateMangaCategories(1, {}, {}).variables["add"] == nlohmann::json::array());
    }

    TEST_CASE("WP-24: the category answers (as the test server sent them, D54)") {
        const auto list = gql::readManageCategories(answer(R"({"data": {"categories": {"nodes": [
            {"id": 0, "name": "Default", "order": 0, "default": true, "mangas": {"totalCount": 2}},
            {"id": 1, "name": "Reading", "order": 1, "default": false, "mangas": {"totalCount": 3}}], "totalCount": 2}}})"));
        REQUIRE(list.ok());
        REQUIRE(list.value().size() == 2);
        CHECK(list.value()[0].mangaCount == 2);
        CHECK(list.value()[1].mangaCount == 3);
        // The plain Categories answer has no counts.
        const auto plain = gql::readCategories(answer(R"({"data": {"categories": {"nodes": [{"id": 1, "name": "R"}]}}})"));
        REQUIRE(plain.ok());
        CHECK(plain.value()[0].mangaCount == -1);

        const auto created = gql::readCreateCategory(answer(R"({"data": {"createCategory": {"category":
            {"id": 33, "name": "New One", "order": 5, "default": false}}}})"));
        REQUIRE(created.ok());
        CHECK(created.value().id == 33);
        CHECK(created.value().order == 5);
        // A taken name: the server's words without its stack trace.
        const auto taken = gql::readCreateCategory(answer(R"({"data": {"createCategory": null}, "errors": [{"message":
            "Exception while fetching data (/createCategory) : 'name' must be unique\r\n\r\njava.lang.IllegalArgumentException: x\r\n\tat a.b"}]})"));
        REQUIRE_FALSE(taken.ok());
        CHECK(taken.error().message == "'name' must be unique");

        const auto renamed = gql::readRenameCategory(answer(R"({"data": {"updateCategory": {"category":
            {"id": 33, "name": "Renamed", "order": 5}}}})"));
        REQUIRE(renamed.ok());
        CHECK(renamed.value().name == "Renamed");

        const auto moved = gql::readMoveCategory(answer(R"({"data": {"updateCategoryOrder": {"categories": [
            {"id": 0, "name": "Default", "order": 0}, {"id": 2, "name": "Long series", "order": 2},
            {"id": 1, "name": "Reading", "order": 1}]}}})"));
        REQUIRE(moved.ok());
        REQUIRE(moved.value().size() == 3);
        CHECK(moved.value()[1].id == 1);  // sorted by order
        CHECK_FALSE(gql::readMoveCategory(answer(R"({"data": {"updateCategoryOrder": null}, "errors": [{"message":
            "Exception while fetching data (/updateCategoryOrder) : 'order' must not be <= 0\r\n"}]})")).ok());

        const auto deleted = gql::readDeleteCategory(answer(R"({"data": {"deleteCategory": {"category": {"id": 34},
            "mangas": [{"id": 1}, {"id": 2}]}}})"));
        REQUIRE(deleted.ok());
        CHECK(deleted.value() == 2);
        const auto nothing = gql::readDeleteCategory(answer(R"({"data": {"deleteCategory": {"category": null, "mangas": []}}})"));
        REQUIRE_FALSE(nothing.ok());
        CHECK(nothing.error().kind == ErrorKind::Server);

        const auto set = gql::readUpdateMangaCategories(answer(R"({"data": {"updateMangaCategories": {"manga":
            {"id": 1, "categories": {"nodes": [{"id": 1, "name": "Reading"}, {"id": 2, "name": "Long series"}]}}}}})"));
        REQUIRE(set.ok());
        REQUIRE(set.value().size() == 2);
        CHECK(set.value()[1].name == "Long series");
        const auto none = gql::readUpdateMangaCategories(answer(R"({"data": {"updateMangaCategories": {"manga":
            {"id": 1, "categories": {"nodes": []}}}}})"));
        REQUIRE(none.ok());
        CHECK(none.value().empty());
        CHECK_FALSE(gql::readUpdateMangaCategories(answer(R"({"data": {"updateMangaCategories": null}, "errors": [
            {"message": "Exception while fetching data (/updateMangaCategories) : Referential integrity"}]})")).ok());
    }
}
