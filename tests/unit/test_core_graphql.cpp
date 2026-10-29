#include <algorithm>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "Fixtures.hpp"
#include "core/graphql/Documents.hpp"
#include "core/graphql/Gql.hpp"
#include "core/graphql/Operations.hpp"

using namespace snx::core;

namespace {

GqlResponse recorded(const std::string& name) {
    auto r = parseGqlResponse(readFixture("responses/" + name + ".json"));
    REQUIRE_MESSAGE(r.ok(), name << ": " << (r.ok() ? "" : r.error().message));
    return r.value();
}

GqlResponse answer(const char* body) {
    auto r = parseGqlResponse(body);
    REQUIRE(r.ok());
    return r.value();
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("every .graphql file is embedded, named after its operation, without comment lines") {
        const auto& docs = gql::documents();
        const std::vector<std::string> expected = {"AboutServer",       "Categories",           "Chapters",
                                                   "FetchChapterPages", "FetchMangaAndChapters", "LibraryByCategory",
                                                   "Login",             "Manga",                "RefreshToken",
                                                   "UpdateChapterProgress", "UpdateChapters"};
        REQUIRE(docs.size() == expected.size());
        for (std::size_t i = 0; i < docs.size(); ++i) {
            CAPTURE(expected[i]);
            CHECK(docs[i].name == expected[i]);
            const std::string text(docs[i].text);
            const bool named = text.rfind("query " + expected[i], 0) == 0 || text.rfind("mutation " + expected[i], 0) == 0;
            CHECK(named);
            CHECK(text.find("\n#") == std::string::npos);
            CHECK(text.find("\n\n") == std::string::npos);
            CHECK(gql::document(expected[i]).text == docs[i].text);
        }
        CHECK(gql::document("NoSuchOperation").name.empty());
        CHECK(gql::document("NoSuchOperation").text.empty());
    }

    TEST_CASE("request builders: document, mutation flag and variables") {
        const auto about = gql::aboutServer();
        CHECK(about.name == "AboutServer");
        CHECK_FALSE(about.mutation);
        CHECK(about.variables == nlohmann::json::object());
        CHECK(gql::categories().name == "Categories");

        const auto library = gql::libraryByCategory(2, 60, 120);
        CHECK(library.name == "LibraryByCategory");
        CHECK(library.variables["filter"] == nlohmann::json::parse(R"({"categoryId": {"equalTo": 2}})"));
        CHECK(library.variables["first"] == 60);
        CHECK(library.variables["offset"] == 120);
        // The Default category has no entries of its own: "in no category".
        CHECK(gql::libraryByCategory(kDefaultCategoryId, 60, 0).variables["filter"] ==
              nlohmann::json::parse(R"({"categoryId": {"isNull": true}})"));
        const auto clamped = gql::libraryByCategory(1, 0, -5);
        CHECK(clamped.variables["first"] == 1);
        CHECK(clamped.variables["offset"] == 0);

        CHECK(gql::manga(7).variables == nlohmann::json::parse(R"({"id": 7})"));

        const auto chapters = gql::chapters(4, 100, 200);
        CHECK(chapters.variables == nlohmann::json::parse(R"({"mangaId": 4, "order": "DESC", "first": 100, "offset": 200})"));
        CHECK(gql::chapters(4, 100, 0, false).variables["order"] == "ASC");

        const auto pages = gql::fetchChapterPages(6);
        CHECK(pages.mutation);
        CHECK(pages.variables == nlohmann::json::parse(R"({"chapterId": 6})"));
        CHECK(gql::fetchChapterPages(6, "image/jpeg").variables["format"] == "image/jpeg");

        const auto progress = gql::updateChapterProgress(ReadProgress{3, 4, false, -1});
        CHECK(progress.mutation);
        CHECK(progress.variables == nlohmann::json::parse(R"({"id": 3, "patch": {"lastPageRead": 4}})"));
        CHECK(gql::updateChapterProgress(ReadProgress{3, 9, true, 10}).variables["patch"] ==
              nlohmann::json::parse(R"({"lastPageRead": 9, "isRead": true})"));
        CHECK(gql::updateChapterProgress(ReadProgress{3, -2, false, 10}).variables["patch"]["lastPageRead"] == 0);

        const auto login = gql::login("snx", "p\"ss");
        CHECK(login.mutation);
        CHECK(login.variables == nlohmann::json::parse(R"({"username": "snx", "password": "p\"ss"})"));
        CHECK(gql::refreshToken("r1").variables == nlohmann::json::parse(R"({"refreshToken": "r1"})"));
    }

    TEST_CASE("request body is {query, variables} and never throws") {
        const auto body = nlohmann::json::parse(gql::manga(1).body());
        CHECK(body["query"].get<std::string>().rfind("query Manga(", 0) == 0);
        CHECK(body["variables"]["id"] == 1);
        CHECK(body.size() == 2);

        // A password with invalid UTF-8 (a broken keyboard string) is replaced, not thrown on.
        std::string text;
        CHECK_NOTHROW(text = gql::login("snx", "bad\xff").body());
        CHECK(nlohmann::json::parse(text)["variables"]["password"].get<std::string>().rfind("bad", 0) == 0);

        GqlRequest odd = gql::aboutServer();
        odd.variables = nlohmann::json::array();  // not an object: sent as {}
        CHECK(nlohmann::json::parse(odd.body())["variables"] == nlohmann::json::object());
    }

    TEST_CASE("answers read from recorded responses") {
        auto about = gql::readAboutServer(recorded("about-server"));
        REQUIRE(about.ok());
        CHECK(about.value().version == "v2.4.2366");

        auto categories = gql::readCategories(recorded("categories"));
        REQUIRE(categories.ok());
        CHECK(categories.value().size() == 5);  // Default, Reading, Long series, Big shelf, Empty (seed.ts)

        auto library = gql::readLibraryByCategory(recorded("library"));
        REQUIRE(library.ok());
        CHECK(library.value().nodes.size() == 156);  // 6 fixture series + the 150 of the Big shelf

        auto manga = gql::readManga(recorded("manga-details"));
        REQUIRE(manga.ok());
        CHECK(manga.value().title == "Alpha");

        auto chapters = gql::readChapters(recorded("manga-chapters"));
        REQUIRE(chapters.ok());
        CHECK(chapters.value().nodes.size() == 3);

        auto pages = gql::readFetchChapterPages(recorded("fetch-chapter-pages"));
        REQUIRE(pages.ok());
        CHECK(pages.value().pages.size() == 10);

        auto progress = gql::readUpdateChapterProgress(recorded("update-chapter-progress"));
        REQUIRE(progress.ok());
        CHECK(progress.value().lastPageRead == 4);

        auto tokens = gql::readLogin(recorded("login"));
        REQUIRE(tokens.ok());
        CHECK(tokens.value().refreshToken == "<refresh-token>");

        auto access = gql::readRefreshToken(recorded("refresh-token"));
        REQUIRE(access.ok());
        CHECK(access.value() == "<access-token>");
    }

    TEST_CASE("our own documents' answers, recorded from the test server (app-*.json)") {
        auto about = gql::readAboutServer(recorded("app-about-server"));
        REQUIRE(about.ok());
        CHECK(isSupportedServer(about.value()));

        auto categories = gql::readCategories(recorded("app-categories"));
        REQUIRE(categories.ok());
        REQUIRE(categories.value().size() == 5);
        CHECK(categories.value()[0].isDefault);

        auto reading = gql::readLibraryByCategory(recorded("app-library-reading"));
        REQUIRE(reading.ok());
        REQUIRE(reading.value().nodes.size() == 2);
        CHECK(reading.value().nodes[0].title == "Alpha");
        CHECK(reading.value().nodes[0].status == MangaStatus::Ongoing);
        CHECK(reading.value().nodes[0].unreadCount == 3);
        CHECK(reading.value().nodes[1].title == "Beta RTL");
        CHECK_FALSE(reading.value().hasNextPage);

        // The Default category: the manga in no category.
        auto byDefault = gql::readLibraryByCategory(recorded("app-library-default"));
        REQUIRE(byDefault.ok());
        REQUIRE(byDefault.value().nodes.size() == 2);
        CHECK(byDefault.value().nodes[0].title == "Broken");
        CHECK(byDefault.value().totalCount == 2);

        auto paged = gql::readLibraryByCategory(recorded("app-library-paged"));
        REQUIRE(paged.ok());
        CHECK(paged.value().nodes.size() == 1);
        CHECK(paged.value().totalCount == 2);
        CHECK(paged.value().hasNextPage);

        auto manga = gql::readManga(recorded("app-manga"));
        REQUIRE(manga.ok());
        CHECK(manga.value().artist == "SNX Fixture Artist");
        REQUIRE(manga.value().source.has_value());
        CHECK(manga.value().source->id == kLocalSourceId);
        REQUIRE(manga.value().firstUnreadChapter.has_value());
        CHECK(manga.value().firstUnreadChapter->name == "Chapter 1");
        CHECK(manga.value().firstUnreadChapter->sourceOrder == 1);

        auto chapters = gql::readChapters(recorded("app-chapters"));
        REQUIRE(chapters.ok());
        REQUIRE(chapters.value().nodes.size() == 3);
        const Chapter& newest = chapters.value().nodes[0];
        CHECK(newest.name == "Chapter 3");
        CHECK(newest.mangaId == 1);
        CHECK(newest.uploadDate == 1767312180000LL);
        CHECK(newest.scanlator.empty());
        CHECK(newest.pageCount == -1);

        auto chapterPage = gql::readChapters(recorded("app-chapters-paged"));
        REQUIRE(chapterPage.ok());
        REQUIRE(chapterPage.value().nodes.size() == 3);
        CHECK(chapterPage.value().nodes[0].name == "Chapter 4");  // oldest first, offset 3
        CHECK(chapterPage.value().totalCount == 300);
        CHECK(chapterPage.value().hasNextPage);

        auto pages = gql::readFetchChapterPages(recorded("app-fetch-chapter-pages"));
        REQUIRE(pages.ok());
        CHECK(pages.value().pages.size() == 10);
        CHECK(pages.value().chapter.pageCount == 10);
        CHECK(pages.value().chapter.sourceOrder == 1);
        CHECK_FALSE(pages.value().syncConflict.has_value());

        auto jpeg = gql::readFetchChapterPages(recorded("app-fetch-chapter-pages-jpeg"));
        REQUIRE(jpeg.ok());
        CHECK(jpeg.value().pages[0] == "/api/v1/manga/1/chapter/1/page/0?format=image%2Fjpeg");

        auto progress = gql::readUpdateChapterProgress(recorded("app-update-chapter-progress"));
        REQUIRE(progress.ok());
        CHECK(progress.value().lastPageRead == 2);  // capped to pageCount (2) by the server
        CHECK(progress.value().pageCount == 2);

        auto tokens = gql::readLogin(recorded("app-login"));
        REQUIRE(tokens.ok());
        CHECK(tokens.value().accessToken == "<access-token>");
        auto access = gql::readRefreshToken(recorded("app-refresh-token"));
        REQUIRE(access.ok());
        CHECK(access.value() == "<access-token>");
    }

    TEST_CASE("recorded error answers: not found, wrong password, unauthorized") {
        const auto notFound = recorded("error-manga-not-found");
        CHECK(notFound.data.is_null());
        REQUIRE(notFound.errors.size() == 1);
        CHECK(notFound.errors[0].path == "manga");
        CHECK_FALSE(notFound.unauthorized());
        auto manga = gql::readManga(notFound);
        REQUIRE_FALSE(manga.ok());
        CHECK(manga.error().kind == ErrorKind::Server);
        CHECK(manga.error().message.find("non null type") != std::string::npos);

        const auto wrong = recorded("login-wrong-password");
        REQUIRE(wrong.errors.size() == 1);
        CHECK(wrong.errors[0].message == "Incorrect username or password.");
        CHECK(wrong.errors[0].path == "login");
        auto login = gql::readLogin(wrong);
        REQUIRE_FALSE(login.ok());
        CHECK(login.error().kind == ErrorKind::Server);
        CHECK(login.error().message == "Incorrect username or password.");

        const auto unauthorized = recorded("unauthorized");
        CHECK(unauthorized.unauthorized());
        CHECK(unauthorized.errors[0].message == "Unauthorized");
        auto categories = gql::readCategories(unauthorized);
        REQUIRE_FALSE(categories.ok());
        CHECK(categories.error().kind == ErrorKind::Unauthorized);
    }

    TEST_CASE("parseGqlResponse: bodies that aren't GraphQL answers") {
        for (const char* bad : {"", "Unauthorized", "[]", "{}", "\"data\"", "{\"data\": 1", "{\"other\": 1}"}) {
            CAPTURE(bad);
            auto r = parseGqlResponse(bad);
            REQUIRE_FALSE(r.ok());
            CHECK(r.error().kind == ErrorKind::Parse);
        }
        JsonLimits tiny;
        tiny.maxBytes = 8;
        CHECK_FALSE(parseGqlResponse(R"({"data": {}})", tiny).ok());
        tiny = JsonLimits{};
        tiny.maxDepth = 2;
        CHECK_FALSE(parseGqlResponse(R"({"data": {"a": {"b": 1}}})", tiny).ok());
    }

    TEST_CASE("partial data: the data is used, the errors stay visible") {
        const auto partial = answer(R"({"data": {"manga": {"id": 1, "title": "Alpha", "thumbnailUrl": null}},
            "errors": [{"message": "Exception while fetching data (/manga/thumbnailUrl) : boom", "path": ["manga", "thumbnailUrl"]}]})");
        auto manga = gql::readManga(partial);
        REQUIRE(manga.ok());
        CHECK(manga.value().title == "Alpha");
        REQUIRE(partial.errors.size() == 1);
        CHECK(partial.errors[0].message == "boom");
        CHECK(partial.errors[0].path == "manga/thumbnailUrl");
    }

    TEST_CASE("readers: missing data without errors is a Parse error; wrong shapes too") {
        auto missing = gql::readManga(answer(R"({"data": {}})"));
        REQUIRE_FALSE(missing.ok());
        CHECK(missing.error().kind == ErrorKind::Parse);
        CHECK(missing.error().message.find("manga") != std::string::npos);

        CHECK_FALSE(gql::readManga(answer(R"({"data": {"manga": []}})")).ok());
        CHECK_FALSE(gql::readCategories(answer(R"({"data": {"categories": {"nodes": 5}}})")).ok());
        CHECK_FALSE(gql::readChapters(answer(R"({"data": null})")).ok());
        CHECK_FALSE(gql::readUpdateChapterProgress(answer(R"({"data": {"updateChapter": {}}})")).ok());
        CHECK_FALSE(gql::readUpdateChapterProgress(answer(R"({"data": {}})")).ok());
        CHECK_FALSE(gql::readRefreshToken(answer(R"({"data": {"refreshToken": {"accessToken": ""}}})")).ok());
        CHECK_FALSE(gql::readRefreshToken(answer(R"({"data": {}})")).ok());
        CHECK_FALSE(gql::readLibraryByCategory(answer(R"({"data": {"mangas": null}})")).ok());
        CHECK_FALSE(gql::readFetchChapterPages(answer(R"({"data": {"fetchChapterPages": {"pages": null}}})")).ok());
        CHECK_FALSE(gql::readAboutServer(answer(R"({"data": {"aboutServer": "v2"}})")).ok());
    }

    TEST_CASE("errors: odd shapes, paths with indices, and unauthorized wins") {
        const auto odd = answer(R"({"data": null, "errors": [{"message": 5}, {"path": ["a", 0, "b", 1.5, true]},
            {"message": "Exception while fetching data (/x) : Unauthorized\r\n\r\njava..."}]})");
        REQUIRE(odd.errors.size() == 3);
        CHECK(odd.errors[0].message == "the server reported an error");
        CHECK(odd.errors[1].path == "a/0/b//");
        CHECK(odd.unauthorized());
        const Error first = odd.firstError();
        CHECK(first.kind == ErrorKind::Unauthorized);
        CHECK(first.message == "Unauthorized");

        const auto notList = answer(R"({"errors": {"message": "x"}})");
        REQUIRE(notList.errors.size() == 1);
        CHECK(notList.firstError().kind == ErrorKind::Server);

        CHECK(answer(R"({"data": {}, "errors": null})").errors.empty());
        CHECK(GqlResponse{}.firstError().kind == ErrorKind::Server);
    }

    TEST_CASE("cleanServerMessage: first line, no prefix, no control characters, at most 300 bytes") {
        CHECK(cleanServerMessage("Exception while fetching data (/login) : Incorrect username or password.\r\n\r\njava.lang.Exception"
                                 ": x\r\n\tat a.b(C.kt:43)") == "Incorrect username or password.");
        CHECK(cleanServerMessage("plain message") == "plain message");
        CHECK(cleanServerMessage("  spaced\t ") == "spaced");
        CHECK(cleanServerMessage("Exception while fetching data (/x without end") == "Exception while fetching data (/x without end");
        CHECK(cleanServerMessage("bell\x07 and \x1b[31mred") == "bell  and  [31mred");
        CHECK(cleanServerMessage("") == "the server reported an error");
        CHECK(cleanServerMessage("\r\nsecond line") == "the server reported an error");

        // 299 ASCII bytes and then a 3-byte character: the cut must not split it.
        const std::string longText = std::string(299, 'a') + "\xE6\x97\xA5" + std::string(100, 'b');
        const std::string cleaned = cleanServerMessage(longText);
        CHECK(cleaned == std::string(299, 'a'));
        CHECK(cleanServerMessage(std::string(1000, 'z')).size() == 300);
    }
    TEST_CASE("WP-10: updateChapters and fetchMangaAndChapters, requests and answers") {
        ChapterPatch read;
        read.isRead = true;
        const auto mark = gql::updateChapters({3, 4}, read);
        CHECK(mark.name == "UpdateChapters");
        CHECK(mark.mutation);
        CHECK(mark.variables == nlohmann::json::parse(R"({"ids": [3, 4], "patch": {"isRead": true}})"));
        ChapterPatch unread;
        unread.isRead = false;
        unread.lastPageRead = -2;
        unread.isBookmarked = true;
        CHECK(gql::updateChapters({7}, unread).variables["patch"] ==
              nlohmann::json::parse(R"({"isRead": false, "isBookmarked": true, "lastPageRead": 0})"));
        CHECK(gql::updateChapters({}, ChapterPatch()).variables ==
              nlohmann::json::parse(R"({"ids": [], "patch": {}})"));

        const auto refresh = gql::fetchMangaAndChapters(9);
        CHECK(refresh.name == "FetchMangaAndChapters");
        CHECK(refresh.mutation);
        CHECK(refresh.variables == nlohmann::json::parse(R"({"id": 9})"));

        auto changed = gql::readUpdateChapters(answer(R"({"data": {"updateChapters": {"chapters": [
            {"id": 3, "isRead": true, "isBookmarked": false, "lastPageRead": 0, "pageCount": 10, "lastReadAt": "1761696000"},
            {"no": "id"}]}}})"));
        REQUIRE(changed.ok());
        REQUIRE(changed.value().size() == 1);  // the broken entry is left out
        CHECK(changed.value()[0].isRead);
        CHECK(changed.value()[0].pageCount == 10);
        CHECK(changed.value()[0].lastReadAt == 1761696000);
        CHECK_FALSE(gql::readUpdateChapters(answer(R"({"data": {"updateChapters": {}}})")).ok());
        CHECK(gql::readUpdateChapters(answer(R"({"data": null, "errors": [{"message": "nope"}]})")).error().message ==
              "nope");

        auto manga = gql::readFetchMangaAndChapters(answer(
            R"({"data": {"fetchMangaAndChapters": {"manga": {"id": 9, "title": "T", "genre": []}, "chapters": [{"id": 1}]}}})"));
        REQUIRE(manga.ok());
        CHECK(manga.value().id == 9);
        CHECK_FALSE(gql::readFetchMangaAndChapters(answer(R"({"data": {"fetchMangaAndChapters": {}}})")).ok());

        // Recorded from the test server (tools/record-fixtures).
        auto recordedChanged = gql::readUpdateChapters(recorded("app-update-chapters"));
        REQUIRE(recordedChanged.ok());
        REQUIRE(recordedChanged.value().size() == 2);
        CHECK_FALSE(recordedChanged.value()[0].isBookmarked);
        auto recordedManga = gql::readFetchMangaAndChapters(recorded("app-fetch-manga-and-chapters"));
        REQUIRE(recordedManga.ok());
        CHECK(recordedManga.value().title == "Broken");
        CHECK(gql::readChapters(recorded("app-chapters")).value().nodes[0].lastReadAt == 0);  // never read
    }
}
