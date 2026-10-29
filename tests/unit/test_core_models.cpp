#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "Fixtures.hpp"
#include "core/models/FromJson.hpp"

using namespace snx::core;

namespace {

// The "data" object of a recorded answer.
nlohmann::json recordedData(const std::string& name) {
    auto parsed = parseJson(readFixture("responses/" + name + ".json"));
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value().contains("data"));
    return parsed.value()["data"];
}

nlohmann::json obj(const char* text) {
    return nlohmann::json::parse(text);
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("ServerInfo from the recorded aboutServer answer") {
        auto info = fromJson<ServerInfo>(recordedData("about-server")["aboutServer"]);
        REQUIRE(info.ok());
        CHECK(info.value().name == "Suwayomi-Server");
        CHECK(info.value().version == "v2.4.2366");
        CHECK(info.value().buildType == "Stable");
        CHECK(info.value().buildTime == 1790737255);
        CHECK(isSupportedServer(info.value()));

        CHECK_FALSE(fromJson<ServerInfo>(nlohmann::json::array()).ok());
        auto empty = fromJson<ServerInfo>(nlohmann::json::object());
        REQUIRE(empty.ok());
        CHECK(empty.value().version.empty());
        CHECK_FALSE(isSupportedServer(empty.value()));
    }

    TEST_CASE("server versions: parsing and the D9 minimum") {
        struct Case {
            const char* text;
            bool known;
            int major, minor, patch;
        };
        const Case cases[] = {
            {"v2.4.2366", true, 2, 4, 2366}, {"2.4.2366", true, 2, 4, 2366}, {"V2.4.2366", true, 2, 4, 2366},
            {"v2.4", true, 2, 4, 0},         {"v2.4.2400-preview", true, 2, 4, 2400}, {"v10.0.1", true, 10, 0, 1},
            {"", false, 0, 0, 0},            {"v", false, 0, 0, 0},     {"v2", false, 0, 0, 0},
            {"v2.", false, 0, 0, 0},         {"v2.4.", false, 0, 0, 0}, {"v2.4.x", false, 0, 0, 0},
            {"v2.4.1.1", false, 0, 0, 0},    {"abc", false, 0, 0, 0},   {"v99999999999.1.1", false, 0, 0, 0},
            {"v2.4 2366", false, 0, 0, 0},
        };
        for (const auto& c : cases) {
            CAPTURE(c.text);
            const ServerVersion v = parseServerVersion(c.text);
            CHECK(v.known == c.known);
            if (c.known) {
                CHECK(v.major == c.major);
                CHECK(v.minor == c.minor);
                CHECK(v.patch == c.patch);
            }
        }
        const char* supported[] = {"v2.4.2366", "v2.4.2367", "v2.5.0", "v3.0.0", "v2.5"};
        const char* unsupported[] = {"v2.4.2365", "v2.3.9999", "v1.9.9999", "v2.4", "nonsense", ""};
        for (const char* v : supported) {
            CAPTURE(v);
            CHECK(isSupportedServer(ServerInfo{"Suwayomi-Server", v, "Stable", 0}));
        }
        for (const char* v : unsupported) {
            CAPTURE(v);
            CHECK_FALSE(isSupportedServer(ServerInfo{"Suwayomi-Server", v, "Stable", 0}));
        }
    }

    TEST_CASE("Category from the recorded categories answer") {
        auto list = nodeListFromJson<Category>(recordedData("categories")["categories"]);
        REQUIRE(list.ok());
        const auto& c = list.value().nodes;
        REQUIRE(c.size() == 5);  // Default, Reading, Long series, Big shelf, Empty (seed.ts)
        CHECK(list.value().totalCount == 5);
        CHECK(list.value().skipped == 0);
        CHECK(c[0].id == kDefaultCategoryId);
        CHECK(c[0].name == "Default");
        CHECK(c[0].isDefault);
        CHECK(c[1].id == 1);
        CHECK(c[1].name == "Reading");
        CHECK(c[1].order == 1);
        CHECK_FALSE(c[1].isDefault);
        CHECK(c[2].name == "Long series");

        // Without the "default" flag, id 0 is still the Default category.
        CHECK(fromJson<Category>(obj(R"({"id": 0})")).value().isDefault);
        CHECK_FALSE(fromJson<Category>(obj(R"({"id": 4})")).value().isDefault);
        CHECK_FALSE(fromJson<Category>(obj(R"({"name": "no id"})")).ok());
    }

    TEST_CASE("Manga from the recorded details answer (all fields, source, categories)") {
        auto m = fromJson<Manga>(recordedData("manga-details")["manga"]);
        REQUIRE(m.ok());
        const Manga& manga = m.value();
        CHECK(manga.id == 1);
        CHECK(manga.title == "Alpha");
        CHECK(manga.author == "SNX Fixture Author");
        CHECK(manga.artist == "SNX Fixture Artist");
        CHECK(manga.description.find("Fixture series") == 0);
        CHECK(manga.genre == std::vector<std::string>{"Action", "Test"});
        CHECK(manga.status == MangaStatus::Ongoing);
        CHECK(manga.thumbnailUrl == "/api/v1/manga/1/thumbnail");
        CHECK(manga.inLibrary);
        CHECK(manga.unreadCount == 3);
        REQUIRE(manga.source.has_value());
        CHECK(manga.source->id == kLocalSourceId);
        CHECK(manga.source->displayName == "Local source");
        REQUIRE(manga.categories.size() == 1);
        CHECK(manga.categories[0].id == 1);
        CHECK(manga.categories[0].name == "Reading");
        CHECK_FALSE(manga.firstUnreadChapter.has_value());
    }

    TEST_CASE("Manga with a Unicode title and null details") {
        auto m = fromJson<Manga>(recordedData("manga-details-unicode")["manga"]);
        REQUIRE(m.ok());
        CHECK(m.value().title == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x82\xBF\xE3\x82\xA4\xE3\x83\x88\xE3\x83\xAB \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88");
        CHECK(m.value().author.empty());
        CHECK(m.value().description.empty());
        CHECK(m.value().genre.empty());
        CHECK(m.value().categories.empty());
        CHECK_FALSE(m.value().source.has_value());
    }

    TEST_CASE("the recorded library: a node list of manga with category ids and paging") {
        auto list = nodeListFromJson<Manga>(recordedData("library")["mangas"]);
        REQUIRE(list.ok());
        const auto& l = list.value();
        REQUIRE(l.nodes.size() == 156);  // 6 fixture series + the 150 of the Big shelf
        CHECK(l.totalCount == 156);
        CHECK_FALSE(l.hasNextPage);
        CHECK(l.endCursor.find("156-") == 0);
        CHECK(l.nodes[0].title == "Alpha");
        REQUIRE(l.nodes[0].categories.size() == 1);
        CHECK(l.nodes[0].categories[0].id == 1);
        CHECK(l.nodes[0].categories[0].name.empty());
        CHECK(l.nodes[2].title == "Broken");
        CHECK(l.nodes[2].categories.empty());
        CHECK(l.nodes[3].unreadCount == 300);

        auto page = nodeListFromJson<Manga>(recordedData("library-page")["mangas"]);
        REQUIRE(page.ok());
        CHECK(page.value().nodes.size() == 2);
        CHECK(page.value().totalCount == 156);
        CHECK(page.value().hasNextPage);
        CHECK(page.value().endCursor == "4-Delta Big");
    }

    TEST_CASE("Chapter from the recorded chapter lists") {
        auto list = nodeListFromJson<Chapter>(recordedData("manga-chapters")["chapters"]);
        REQUIRE(list.ok());
        const auto& c = list.value().nodes;
        REQUIRE(c.size() == 3);
        CHECK(c[0].id == 3);
        CHECK(c[0].name == "Chapter 3");
        CHECK(c[0].chapterNumber == 3.0);
        CHECK(c[0].sourceOrder == 3);
        CHECK(c[0].uploadDate == 1767312180000LL);
        CHECK_FALSE(c[0].isRead);
        CHECK_FALSE(c[0].isBookmarked);
        CHECK_FALSE(c[0].isDownloaded);
        CHECK(c[0].lastPageRead == 0);
        CHECK(c[0].pageCount == -1);
        CHECK(c[2].pageCount == 10);

        auto big = nodeListFromJson<Chapter>(recordedData("manga-chapters-page")["chapters"]);
        REQUIRE(big.ok());
        CHECK(big.value().nodes.size() == 50);
        CHECK(big.value().totalCount == 300);
        CHECK(big.value().hasNextPage);
        CHECK(big.value().nodes[0].name == "Chapter 300");
    }

    TEST_CASE("PageList from the recorded fetchChapterPages answers") {
        auto pages = fromJson<PageList>(recordedData("fetch-chapter-pages")["fetchChapterPages"]);
        REQUIRE(pages.ok());
        REQUIRE(pages.value().pages.size() == 10);
        CHECK(pages.value().pages[0] == "/api/v1/manga/1/chapter/1/page/0");
        CHECK(pages.value().pages[9] == "/api/v1/manga/1/chapter/1/page/9");
        CHECK(pages.value().chapter.id == 1);
        CHECK(pages.value().chapter.pageCount == 10);
        CHECK_FALSE(pages.value().syncConflict.has_value());

        auto broken = fromJson<PageList>(recordedData("fetch-chapter-pages-broken")["fetchChapterPages"]);
        REQUIRE(broken.ok());
        CHECK(broken.value().pages.size() == 5);  // corrupt, empty and 1x1 pages are still listed
        CHECK(broken.value().chapter.id == 6);
    }

    TEST_CASE("PageList keeps page indices when entries are odd, and reads a sync conflict") {
        auto p = fromJson<PageList>(obj(R"({"pages": ["/p/0", 5, null, "/p/3"], "syncConflict": {"deviceName": "KOReader",
                                            "remotePage": 7}, "chapter": "not an object"})"));
        REQUIRE(p.ok());
        CHECK(p.value().pages == std::vector<std::string>{"/p/0", "", "", "/p/3"});
        REQUIRE(p.value().syncConflict.has_value());
        CHECK(p.value().syncConflict->deviceName == "KOReader");
        CHECK(p.value().syncConflict->remotePage == 7);
        CHECK(p.value().chapter.id == 0);

        CHECK(fromJson<PageList>(obj(R"({"pages": []})")).value().pages.empty());
        CHECK_FALSE(fromJson<PageList>(obj(R"({"pages": "x"})")).ok());
        CHECK_FALSE(fromJson<PageList>(obj(R"({})")).ok());
        CHECK_FALSE(fromJson<PageList>(nlohmann::json()).ok());
        CHECK_FALSE(fromJson<SyncConflict>(nlohmann::json(3)).ok());
    }

    TEST_CASE("ReadProgress from the recorded updateChapter answer") {
        auto p = fromJson<ReadProgress>(recordedData("update-chapter-progress")["updateChapter"]["chapter"]);
        REQUIRE(p.ok());
        CHECK(p.value().chapterId == 3);
        CHECK(p.value().lastPageRead == 4);
        CHECK_FALSE(p.value().isRead);
        CHECK(p.value().pageCount == 10);
        CHECK_FALSE(fromJson<ReadProgress>(obj(R"({"lastPageRead": 4})")).ok());
        CHECK_FALSE(fromJson<ReadProgress>(nlohmann::json("x")).ok());
    }

    TEST_CASE("LoginTokens from the recorded login answer") {
        auto t = fromJson<LoginTokens>(recordedData("login")["login"]);
        REQUIRE(t.ok());
        CHECK(t.value().accessToken == "<access-token>");
        CHECK(t.value().refreshToken == "<refresh-token>");
        CHECK_FALSE(fromJson<LoginTokens>(obj(R"({"accessToken": "", "refreshToken": "r"})")).ok());
        CHECK_FALSE(fromJson<LoginTokens>(obj(R"({"refreshToken": "r"})")).ok());
        CHECK_FALSE(fromJson<LoginTokens>(nlohmann::json::array()).ok());
    }

    TEST_CASE("Source: LongString ids as text or number") {
        CHECK(fromJson<Source>(obj(R"({"id": "0", "name": "Local source"})")).value().displayName == "Local source");
        CHECK(fromJson<Source>(obj(R"({"id": "-6795413373893765236"})")).value().id == "-6795413373893765236");
        CHECK(fromJson<Source>(obj(R"({"id": 2499283573021220255})")).value().id == "2499283573021220255");
        CHECK_FALSE(fromJson<Source>(obj(R"({"id": "abc"})")).ok());
        CHECK_FALSE(fromJson<Source>(obj(R"({"id": ""})")).ok());
        CHECK_FALSE(fromJson<Source>(obj(R"({"id": 1.5})")).ok());
        CHECK_FALSE(fromJson<Source>(obj(R"({})")).ok());
        CHECK_FALSE(fromJson<Source>(nlohmann::json(1)).ok());
    }

    TEST_CASE("nasty input: models fail only without an object or an id") {
        for (const char* text : {"null", "[]", "\"manga\"", "5", "true"}) {
            CAPTURE(text);
            const auto value = nlohmann::json::parse(text);
            CHECK_FALSE(fromJson<Manga>(value).ok());
            CHECK_FALSE(fromJson<Chapter>(value).ok());
            CHECK_FALSE(fromJson<Category>(value).ok());
            CHECK_FALSE(fromJson<ServerInfo>(value).ok());
        }
        for (const char* text : {R"({})", R"({"id": null})", R"({"id": "5"})", R"({"id": 1099511627776})", R"({"id": 1.5})",
                                 R"({"id": true})", R"({"id": [1]})"}) {
            CAPTURE(text);
            const auto value = nlohmann::json::parse(text);
            const auto m = fromJson<Manga>(value);
            REQUIRE_FALSE(m.ok());
            CHECK(m.error().kind == ErrorKind::Parse);
            CHECK(m.error().message.find("id") != std::string::npos);
            CHECK_FALSE(fromJson<Chapter>(value).ok());
        }
        CHECK(fromJson<Manga>(obj(R"({"id": 3.0})")).value().id == 3);
    }

    TEST_CASE("nasty input: nulls and wrong types keep the defaults, unknown fields are ignored") {
        auto nulls = fromJson<Manga>(obj(R"({"id": 9, "title": null, "thumbnailUrl": null, "author": null, "genre": null,
            "status": null, "inLibrary": null, "unreadCount": null, "source": null, "categories": null,
            "firstUnreadChapter": null})"));
        REQUIRE(nulls.ok());
        CHECK(nulls.value().title.empty());
        CHECK(nulls.value().status == MangaStatus::Unknown);
        CHECK_FALSE(nulls.value().source.has_value());

        auto wrong = fromJson<Manga>(obj(R"({"id": 9, "title": 5, "genre": "Action", "status": 3, "unreadCount": "7",
            "inLibrary": "yes", "downloadCount": -1.5, "source": {"name": "no id"}, "categories": {"nodes": "x"},
            "firstUnreadChapter": {"name": "no id"}, "brandNewField": {"deep": [1, 2, 3]}})"));
        REQUIRE(wrong.ok());
        const Manga& m = wrong.value();
        CHECK(m.title.empty());
        CHECK(m.genre.empty());
        CHECK(m.status == MangaStatus::Unknown);
        CHECK(m.unreadCount == 0);
        CHECK_FALSE(m.inLibrary);
        CHECK(m.downloadCount == 0);
        CHECK_FALSE(m.source.has_value());
        CHECK(m.categories.empty());
        CHECK_FALSE(m.firstUnreadChapter.has_value());

        auto chapter = fromJson<Chapter>(obj(R"({"id": 1, "chapterNumber": "12", "uploadDate": "99999999999999999999",
            "pageCount": "10", "isRead": 1, "scanlator": null})"));
        REQUIRE(chapter.ok());
        CHECK(chapter.value().chapterNumber == -1);
        CHECK(chapter.value().uploadDate == 0);
        CHECK(chapter.value().pageCount == -1);
        CHECK_FALSE(chapter.value().isRead);
        CHECK(chapter.value().scanlator.empty());

        auto real = fromJson<Chapter>(obj(R"({"id": 1, "chapterNumber": 12.5, "uploadDate": 1767312180000, "scanlator": "Team"})"));
        CHECK(real.value().chapterNumber == 12.5);
        CHECK(real.value().uploadDate == 1767312180000LL);
        CHECK(real.value().scanlator == "Team");
    }

    TEST_CASE("nasty input: Unicode, emoji, escapes and huge strings come through unchanged") {
        auto parsed = parseJson(R"({"id": 1, "title": "é😀 \"quoted\" \\ back\nslash", "genre": ["日"]})");
        REQUIRE(parsed.ok());
        auto m = fromJson<Manga>(parsed.value());
        REQUIRE(m.ok());
        CHECK(m.value().title == "\xC3\xA9\xF0\x9F\x98\x80 \"quoted\" \\ back\nslash");
        CHECK(m.value().genre == std::vector<std::string>{"\xE6\x97\xA5"});

        nlohmann::json huge = {{"id", 1}, {"description", std::string(1'000'000, 'x')}};
        CHECK(fromJson<Manga>(huge).value().description.size() == 1'000'000);
    }

    TEST_CASE("node lists: broken entries are skipped and counted, empty lists are fine") {
        auto list = nodeListFromJson<Manga>(obj(R"({"nodes": [{"id": 1, "title": "A"}, {"title": "no id"}, null, 7,
            {"id": 2}], "totalCount": 5, "pageInfo": {"hasNextPage": true, "endCursor": "2-B"}})"));
        REQUIRE(list.ok());
        CHECK(list.value().nodes.size() == 2);
        CHECK(list.value().skipped == 3);
        CHECK(list.value().totalCount == 5);
        CHECK(list.value().hasNextPage);
        CHECK(list.value().endCursor == "2-B");

        auto empty = nodeListFromJson<Chapter>(obj(R"({"nodes": [], "totalCount": 0})"));
        REQUIRE(empty.ok());
        CHECK(empty.value().nodes.empty());
        CHECK_FALSE(empty.value().hasNextPage);

        // No totalCount: the number of nodes. A null pageInfo: no next page.
        auto bare = nodeListFromJson<Category>(obj(R"({"nodes": [{"id": 1}], "pageInfo": null})"));
        CHECK(bare.value().totalCount == 1);
        CHECK_FALSE(bare.value().hasNextPage);

        CHECK_FALSE(nodeListFromJson<Manga>(obj(R"({"totalCount": 3})")).ok());
        CHECK_FALSE(nodeListFromJson<Manga>(obj(R"({"nodes": {"id": 1}})")).ok());
        CHECK_FALSE(nodeListFromJson<Manga>(nlohmann::json::array()).ok());
    }

    TEST_CASE("manga status names round-trip, unknown names become Unknown") {
        for (const auto status : {MangaStatus::Unknown, MangaStatus::Ongoing, MangaStatus::Completed, MangaStatus::Licensed,
                                  MangaStatus::PublishingFinished, MangaStatus::Cancelled, MangaStatus::OnHiatus}) {
            CHECK(mangaStatusFromName(mangaStatusName(status)) == status);
        }
        CHECK(mangaStatusName(MangaStatus::OnHiatus) == "ON_HIATUS");
        CHECK(mangaStatusFromName("SOMETHING_NEW") == MangaStatus::Unknown);
        CHECK(mangaStatusFromName("ongoing") == MangaStatus::Unknown);
        CHECK(mangaStatusName(static_cast<MangaStatus>(99)) == "UNKNOWN");
    }
}
