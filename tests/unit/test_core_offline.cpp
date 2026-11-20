// WP-18b (D61): reading away from home, the pure rules (src/core/Offline.hpp): the server's clock from the Date header,
// progress.json, the merged chapter and manga the offline screens show, the gap, the title search, the test config keys.
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/Offline.hpp"
#include "core/StripMath.hpp"
#include "core/TestConfig.hpp"

using namespace snx::core;

namespace {

SwitchChapter downloaded(int id, int order, int pages) {
    SwitchChapter c;
    c.id = id;
    c.name = "Chapter " + std::to_string(order);
    c.sourceOrder = order;
    c.formats = std::string(static_cast<std::size_t>(pages), kPageJpeg);
    return c;
}

KnownProgress known(int page, bool read, std::int64_t lastReadAt) {
    KnownProgress k;
    k.server.page = page;
    k.server.read = read;
    k.server.lastReadAt = lastReadAt;
    return k;
}

ProgressFileLoad load(const std::string& text) {
    return readProgressFile(text, JsonLimits{512 * 1024, 64});
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("Offline: parseHttpDate reads RFC 1123 dates only") {
        CHECK(parseHttpDate("Fri, 20 Nov 2026 05:03:18 GMT") == 1795150998);  // the test server's answer, `date +%s`
        CHECK(parseHttpDate("Thu, 01 Jan 1970 00:00:00 GMT") == 0);
        CHECK(parseHttpDate("Tue, 29 Feb 2028 23:59:59 GMT") == 1835481599);  // a leap day
        CHECK(parseHttpDate("  Fri, 20 Nov 2026 05:03:18 GMT\r\n") == 1795150998);  // a header value's spaces
        CHECK(parseHttpDate("Fri, 20 Nov 2026 05:03:60 GMT") == 1795151039);  // a leap second counts as :59
        CHECK_FALSE(parseHttpDate(""));
        CHECK_FALSE(parseHttpDate("Friday, 20-Nov-26 05:03:18 GMT"));  // RFC 850
        CHECK_FALSE(parseHttpDate("Fri Nov 20 05:03:18 2026"));        // asctime
        CHECK_FALSE(parseHttpDate("Fri, 20 Nov 2026 05:03:18 UTC"));
        CHECK_FALSE(parseHttpDate("Xyz, 20 Nov 2026 05:03:18 GMT"));
        CHECK_FALSE(parseHttpDate("Fri, 20 Noo 2026 05:03:18 GMT"));
        CHECK_FALSE(parseHttpDate("Fri, 31 Nov 2026 05:03:18 GMT"));  // November has 30 days
        CHECK_FALSE(parseHttpDate("Thu, 29 Feb 2027 05:03:18 GMT"));  // not a leap year
        CHECK_FALSE(parseHttpDate("Fri, 20 Nov 2026 24:03:18 GMT"));
        CHECK_FALSE(parseHttpDate("Fri, 20 Nov 2026 05:60:18 GMT"));
        CHECK_FALSE(parseHttpDate("Fri, 20 Nov 1969 05:03:18 GMT"));
        CHECK_FALSE(parseHttpDate("Fri, 2x Nov 2026 05:03:18 GMT"));
        CHECK_FALSE(parseHttpDate(std::string(100, ' ') + "Fri, 20 Nov 2026 05:03:18 GMT"));  // over 64 bytes (S6)
    }

    TEST_CASE("Offline: progress.json round trip, tolerant reading, fitting") {
        SwitchProgress progress;
        progress[578] = known(3, false, 1795150998);
        progress[578].server.spotNote = stripSpotValue(3, 0.25);
        progress[579] = known(0, true, 1795150000);
        progress[579].server.bookmarked = true;
        KnownProgress gone = known(1, false, 10);
        gone.gone = true;
        gone.local = LocalProgress{4, true, 0.5, 1795150999};
        progress[600] = gone;
        const std::string text = writeProgressFile(progress);
        CHECK(text.find("\n  {\"id\":578") != std::string::npos);  // one chapter per line
        const ProgressFileLoad back = load(text);
        REQUIRE(back.usable);
        CHECK(back.skipped == 0);
        CHECK(back.progress == progress);
        CHECK(load(writeProgressFile({})).usable);
        CHECK(load(writeProgressFile({})).progress.empty());

        // Broken entries are skipped, unknown fields ignored, the same chapter twice keeps the later one.
        const ProgressFileLoad messy = load(R"({"version": 1, "chapters": [
            {"id": 1, "page": 2, "read": true, "new": [1, 2]},
            {"id": 0, "page": 1}, {"page": 1}, {"id": 2, "page": -1}, "text", {"id": 3},
            {"id": 4, "page": 1, "local": {"page": 9, "spot": 7}},
            {"id": 1, "page": 5}]})");
        REQUIRE(messy.usable);
        CHECK(messy.skipped == 5);
        CHECK(messy.progress.size() == 2);
        CHECK(messy.progress.at(1).server.page == 5);
        REQUIRE(messy.progress.at(4).local);
        CHECK_FALSE(messy.progress.at(4).local->spot);  // outside 0..1
        CHECK_FALSE(load("{\"version\": 1}").usable);
        CHECK_FALSE(load("not json").usable);
        CHECK_FALSE(load("[1, 2]").usable);
        CHECK_FALSE(readProgressFile(text, JsonLimits{16, 64}).usable);  // over the size limit (S6)
        const ProgressFileLoad newer = load(R"({"version": 2, "chapters": []})");
        CHECK(newer.usable);
        CHECK(newer.problems.size() == 1);

        // Fitting: the highest ids go first until the file fits.
        SwitchProgress many;
        for (int id = 1; id <= 50; ++id) {
            many[id] = known(id, false, 1795150000 + id);
        }
        const std::size_t full = writeProgressFile(many).size();
        SwitchProgress fitted = many;
        CHECK(fitProgressFile(fitted, full) == 0);
        CHECK(fitted.size() == 50);
        const int dropped = fitProgressFile(fitted, full / 2);
        CHECK(dropped > 0);
        CHECK(writeProgressFile(fitted).size() <= full / 2);
        CHECK(fitted.count(1) == 1);
        CHECK(fitted.count(50) == 0);
    }

    TEST_CASE("Offline: offlineChapter merges the download, the known progress and a waiting save") {
        const SwitchChapter c = downloaded(578, 3, 10);
        // Nothing known: manga.json's fields, unread at page 1.
        Chapter plain = offlineChapter(c, 12, nullptr, nullptr, 0);
        CHECK(plain.id == 578);
        CHECK(plain.mangaId == 12);
        CHECK(plain.sourceOrder == 3);
        CHECK(plain.pageCount == 10);
        CHECK(plain.lastPageRead == 0);
        CHECK_FALSE(plain.isRead);
        CHECK_FALSE(plain.goneFromServer);
        // What the server said last.
        KnownProgress k = known(4, false, 1000);
        k.server.bookmarked = true;
        k.server.spotNote = stripSpotValue(4, 0.3);
        const Chapter fromServer = offlineChapter(c, 12, &k, nullptr, 0);
        CHECK(fromServer.lastPageRead == 4);
        CHECK(fromServer.lastReadAt == 1000);
        CHECK(fromServer.isBookmarked);
        CHECK(fromServer.stripSpotNote == stripSpotValue(4, 0.3));
        // A waiting save on top: its page and spot, its time on the server's clock.
        WaitingSave w;
        w.chapterId = 578;
        w.page = 7;
        w.spot = 0.75;
        w.madeAt = 2000;
        const Chapter merged = offlineChapter(c, 12, &k, &w, 30);
        CHECK(merged.lastPageRead == 7);
        CHECK_FALSE(merged.isRead);
        CHECK(merged.stripSpotNote == stripSpotValue(7, 0.75));
        CHECK(merged.lastReadAt == 2030);
        // Read stays read; an older save doesn't move the stamp back; a page past the end is kept inside the chapter.
        KnownProgress read = known(9, true, 5000);
        w.spot.reset();
        w.page = 40;
        const Chapter stays = offlineChapter(c, 12, &read, &w, 0);
        CHECK(stays.isRead);
        CHECK(stays.lastReadAt == 5000);
        CHECK(stays.lastPageRead == 9);
        w.read = true;
        CHECK(offlineChapter(c, 12, nullptr, &w, 0).isRead);
        // A gone chapter: tagged, its local progress counts.
        KnownProgress gone = known(2, false, 100);
        gone.gone = true;
        CHECK(offlineChapter(c, 12, &gone, nullptr, 0).goneFromServer);
        CHECK(offlineChapter(c, 12, &gone, nullptr, 0).lastPageRead == 2);
        gone.local = LocalProgress{5, false, 0.5, 900};
        const Chapter local = offlineChapter(c, 12, &gone, nullptr, 10);
        CHECK(local.lastPageRead == 5);
        CHECK(local.stripSpotNote == stripSpotValue(5, 0.5));
        CHECK(local.lastReadAt == 910);
    }

    TEST_CASE("Offline: the shelf's item, the gap, the title search") {
        SwitchManga manga;
        manga.id = 12;
        manga.title = "Alpha";
        manga.author = "A. Author";
        manga.genres = {"Action"};
        manga.status = "ONGOING";
        manga.sourceId = "0";
        manga.sourceName = "Local source";
        manga.thumbnailUrl = "/api/v1/manga/12/thumbnail";
        std::vector<Chapter> chapters(3);
        chapters[0].isRead = true;
        chapters[0].lastReadAt = 300;
        chapters[1].lastReadAt = 900;
        const Manga item = offlineManga(manga, chapters);
        CHECK(item.id == 12);
        CHECK(item.title == "Alpha");
        CHECK(item.author == "A. Author");
        CHECK(item.status == MangaStatus::Ongoing);
        CHECK(item.inLibrary);
        REQUIRE(item.source);
        CHECK(item.source->displayName == "Local source");
        CHECK(item.unreadCount == 2);
        CHECK(item.downloadCount == 3);
        CHECK(item.lastReadAt == 900);

        Chapter one;
        one.sourceOrder = 1;
        Chapter two;
        two.sourceOrder = 2;
        Chapter five;
        five.sourceOrder = 5;
        CHECK(chaptersSkipped(one, two) == 0);
        CHECK(chaptersSkipped(one, five) == 3);
        CHECK(chaptersSkipped(two, five) == 2);
        CHECK(chaptersSkipped(five, one) == 0);
        CHECK(chaptersSkipped(one, one) == 0);

        CHECK(titleMatches("Alpha Manga", "alpha"));
        CHECK(titleMatches("Alpha Manga", "A M"));
        CHECK(titleMatches("Alpha Manga", ""));
        CHECK_FALSE(titleMatches("Alpha Manga", "beta"));
        CHECK(titleMatches("Ünïcode Title", "Ünï"));
        CHECK_FALSE(titleMatches("Ab", "abc"));
    }

    TEST_CASE("Offline: the test config's networkConnected and offlineCheckMs") {
        const testconfig::TestConfig set = testconfig::parse(R"({"networkConnected": false, "offlineCheckMs": 3000})");
        CHECK(set.problems.empty());
        CHECK(set.networkConnected == false);
        CHECK(set.offlineCheckMs == 3000);
        const testconfig::TestConfig none = testconfig::parse("{}");
        CHECK_FALSE(none.networkConnected);
        CHECK_FALSE(none.offlineCheckMs);
        CHECK_FALSE(testconfig::parse(R"({"networkConnected": 1})").problems.empty());
        CHECK_FALSE(testconfig::parse(R"({"offlineCheckMs": 10})").problems.empty());
        CHECK_FALSE(testconfig::parse(R"({"offlineCheckMs": "30000"})").problems.empty());
    }
}
