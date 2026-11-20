// WP-18 (D59): the Switch downloads' pure rules (src/core/SwitchDownloads.hpp): names, the three files, space, the tab's
// view, sizes, and the settings section.
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/AppConfig.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/SwitchDownloads.hpp"
#include "core/TestConfig.hpp"

using namespace snx::core;

namespace {

SwitchChapter downloaded(int id, int order, std::string formats, std::uint64_t bytes) {
    SwitchChapter c;
    c.id = id;
    c.name = "Chapter " + std::to_string(order);
    c.chapterNumber = order;
    c.sourceOrder = order;
    c.formats = std::move(formats);
    c.bytes = bytes;
    c.downloadedAt = 1794534794;
    return c;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("switch downloads: a server's folder is named by its address, the same with or without a trailing /") {
        CHECK(downloadServerUrl("http://10.0.0.2:4567/") == "http://10.0.0.2:4567");
        CHECK(downloadServerUrl("http://10.0.0.2:4567//") == "http://10.0.0.2:4567");
        const std::string key = switchServerKey("http://10.0.0.2:4567");
        CHECK(key.size() == 16);
        CHECK(key.find_first_not_of("0123456789abcdef") == std::string::npos);
        CHECK(switchServerKey("http://10.0.0.2:4567/") == key);
        CHECK(switchServerKey("http://10.0.0.3:4567") != key);
        CHECK(switchServerKey("") == "cbf29ce484222325");  // FNV-1a's offset basis: nothing hashed
        CHECK(switchChapterDir(key, 12, 578) == "downloads/" + key + "/m12/c578");
        CHECK(switchMangaDir(key, 12) == "downloads/" + key + "/m12");
        CHECK(idOfFolder("m12", 'm') == 12);
        CHECK(idOfFolder("c578", 'c') == 578);
        CHECK_FALSE(idOfFolder("c578", 'm'));
        CHECK_FALSE(idOfFolder("m", 'm'));
        CHECK_FALSE(idOfFolder("m0", 'm'));
        CHECK_FALSE(idOfFolder("m12x", 'm'));
        CHECK_FALSE(idOfFolder("m99999999999", 'm'));
        CHECK_FALSE(idOfFolder("manga.json", 'm'));
    }

    TEST_CASE("switch downloads: page files are named by index and format, and only those names read back") {
        CHECK(switchPageName(0, kPageJpeg) == "0000.jpg");
        CHECK(switchPageName(12, kPageWebp) == "0012.webp");
        CHECK(switchPageName(3, kPagePng) == "0003.png");
        CHECK(switchPageName(9999, kPageGif) == "9999.gif");
        CHECK(switchPageName(1, kPageUnusable).empty());
        CHECK(switchPageName(1, 'x').empty());
        const auto page = parsePageName("0012.webp");
        REQUIRE(page);
        CHECK(page->index == 12);
        CHECK(page->format == kPageWebp);
        CHECK_FALSE(parsePageName("0012.webp.tmp"));
        CHECK_FALSE(parsePageName("12.jpg"));
        CHECK_FALSE(parsePageName("0012.jpeg"));
        CHECK_FALSE(parsePageName("cover.img"));
        CHECK_FALSE(parsePageName("manga.json"));
        CHECK_FALSE(parsePageName("10000.jpg"));  // past kSwitchPagesMax
    }

    TEST_CASE("switch downloads: manga.json writes and reads back, broken chapters are skipped and counted") {
        SwitchManga manga;
        manga.id = 12;
        manga.title = "Alpha";
        manga.genres = {"Action", "Drama"};
        manga.status = "ONGOING";
        manga.sourceId = "0";
        manga.sourceName = "Local source";
        manga.thumbnailUrl = "/api/v1/manga/12/thumbnail";
        manga.coverBytes = 1000;
        putChapter(manga, downloaded(579, 2, "jjw-", 300));
        putChapter(manga, downloaded(578, 1, "jjjj", 400));
        REQUIRE(manga.chapters.size() == 2);
        CHECK(manga.chapters.front().id == 578);  // by sourceOrder
        CHECK(manga.bytes() == 1700);
        manga.chapters[1].damaged = {2};

        const std::string text = writeMangaFile(manga);
        const MangaFileLoad load = readMangaFile(text, JsonLimits{});
        REQUIRE(load.usable);
        CHECK(load.problems.empty());
        CHECK(writeMangaFile(load.manga) == text);
        REQUIRE(load.manga.chapter(579));
        CHECK(load.manga.chapter(579)->pages() == 4);
        CHECK(load.manga.chapter(579)->damaged == std::vector<int>{2});

        // A chapter without an id, one with a letter it doesn't know, and a repeat (the later one stays).
        const std::string broken =
            R"({"version":1,"manga":{"id":12,"title":"Alpha"},"chapters":[{"id":0},{"id":5,"formats":"jx"},)"
            R"({"id":6,"sourceOrder":1,"formats":"jj","bytes":10,"damaged":[5,-1,0,0]},)"
            R"({"id":6,"sourceOrder":1,"formats":"jjj","bytes":20}]})";
        const MangaFileLoad tolerant = readMangaFile(broken, JsonLimits{});
        REQUIRE(tolerant.usable);
        CHECK(tolerant.skipped == 2);
        REQUIRE(tolerant.manga.chapters.size() == 1);
        CHECK(tolerant.manga.chapters[0].pages() == 3);
        CHECK(tolerant.manga.chapters[0].damaged.empty());

        const MangaFileLoad damaged = readMangaFile(R"({"version":1,"manga":{"id":12},"chapters":[{"id":6,"formats":"jj","damaged":[5,1,-1,1]}]})", JsonLimits{});
        REQUIRE(damaged.manga.chapters.size() == 1);
        CHECK(damaged.manga.chapters[0].damaged == std::vector<int>{1});  // in range, sorted, no repeats

        CHECK_FALSE(readMangaFile("", JsonLimits{}).usable);
        CHECK_FALSE(readMangaFile(R"({"manga":{"title":"no id"}})", JsonLimits{}).usable);
        CHECK_FALSE(readMangaFile(text.substr(0, text.size() / 2), JsonLimits{}).usable);  // cut short by a power cut
    }

    TEST_CASE("switch downloads: a manga's fields are taken from the server's, cut to their limits") {
        Manga m;
        m.id = 3;
        m.title = std::string(600, 'a');
        m.description = std::string(9000, 'd');
        m.status = MangaStatus::Completed;
        m.source = Source{};
        m.source->id = "77";
        m.source->name = "snx";
        m.source->displayName = "SNX Test (EN)";
        for (int i = 0; i < 70; ++i) {
            m.genre.push_back("g");
        }
        const SwitchManga s = switchMangaFrom(m);
        CHECK(s.title.size() == kSwitchTextMaxBytes);
        CHECK(s.description.size() == kSwitchDescriptionMaxBytes);
        CHECK(s.genres.size() == kSwitchGenresMax);
        CHECK(s.status == "COMPLETED");
        CHECK(s.sourceName == "SNX Test (EN)");
        CHECK(switchMangaFrom(Manga{}).status.empty());

        Chapter c;
        c.id = 9;
        c.name = "Chapter 9";
        c.sourceOrder = 9;
        c.chapterNumber = 9;
        c.isDownloaded = true;
        SwitchChapter sc = switchChapterFrom(c);
        sc.formats = "jjj";
        const Chapter back = chapterFrom(sc, 3);
        CHECK(back.id == 9);
        CHECK(back.mangaId == 3);
        CHECK(back.pageCount == 3);
        CHECK_FALSE(back.isDownloaded);  // the server's flag, not ours
    }

    TEST_CASE("switch downloads: queue.json keeps order, the paused flag and failures; broken entries are skipped") {
        SwitchQueue queue;
        queue.paused = true;
        SwitchQueueEntry a;
        a.mangaId = 12;
        a.mangaTitle = "Alpha";
        a.chapter = downloaded(580, 3, "", 0);
        SwitchQueueEntry b;
        b.mangaId = 158;
        b.mangaTitle = "Comet Courier";
        b.chapter = downloaded(600, 1, "", 0);
        b.failed = true;
        b.failedPage = 1;
        b.pageCount = 6;
        b.error = "the server answered HTTP 500";
        queue.entries = {a, b};
        const std::string text = writeQueueFile(queue);
        const QueueFileLoad load = readQueueFile(text, JsonLimits{}, 1000);
        REQUIRE(load.usable);
        CHECK(load.problems.empty());
        CHECK(load.queue.paused);
        REQUIRE(load.queue.entries.size() == 2);
        CHECK(load.queue.entries[0].chapter.id == 580);
        CHECK(load.queue.entries[1].failed);
        CHECK(load.queue.entries[1].failedPage == 1);
        CHECK(writeQueueFile(load.queue) == text);
        REQUIRE(findEntry(load.queue, 600));
        CHECK_FALSE(findEntry(load.queue, 1));

        const QueueFileLoad capped = readQueueFile(text, JsonLimits{}, 1);
        CHECK(capped.queue.entries.size() == 1);

        const std::string broken = R"({"entries":[{"mangaId":0,"chapter":{"id":1}},{"mangaId":1},)"
                                   R"({"mangaId":1,"chapter":{"id":2},"pageCount":-5,"failedPage":-9},)"
                                   R"({"mangaId":1,"chapter":{"id":2}}]})";
        const QueueFileLoad tolerant = readQueueFile(broken, JsonLimits{}, 1000);
        REQUIRE(tolerant.usable);
        CHECK(tolerant.skipped == 2);
        REQUIRE(tolerant.queue.entries.size() == 1);
        CHECK(tolerant.queue.entries[0].pageCount == 0);
        CHECK(tolerant.queue.entries[0].failedPage == -1);
        CHECK_FALSE(readQueueFile("[]", JsonLimits{}, 10).usable);
    }

    TEST_CASE("switch downloads: server.json holds the server's addresses") {
        const std::string text = writeServerFile({"http://a:4567", "http://b:4567"});
        const auto urls = readServerFile(text, JsonLimits{});
        REQUIRE(urls);
        CHECK(*urls == std::vector<std::string>{"http://a:4567", "http://b:4567"});
        const auto tidy = readServerFile(R"({"urls":["http://a:4567/","http://a:4567","",5]})", JsonLimits{});
        REQUIRE(tidy);
        CHECK(*tidy == std::vector<std::string>{"http://a:4567"});
        CHECK_FALSE(readServerFile("{}", JsonLimits{}));
        CHECK_FALSE(readServerFile("not json", JsonLimits{}));
    }

    TEST_CASE("switch downloads (WP-18c): a second address joins a server's list once; a full list drops its oldest") {
        const std::vector<std::string> one{"http://a:4567"};
        CHECK(addServerUrl(one, "http://b:4567/", "http://a:4567") ==
              std::vector<std::string>{"http://a:4567", "http://b:4567"});
        CHECK(addServerUrl(one, "http://a:4567/", "http://a:4567") == one);  // already there
        CHECK(addServerUrl(one, "", "http://a:4567") == one);
        CHECK(addServerUrl(one, std::string(600, 'x'), "http://a:4567") == one);  // too long to keep
        // Full (16): the oldest that isn't the saved address ("http://h0") goes: h1. The saved one stays first.
        std::vector<std::string> full;
        for (int i = 0; i < static_cast<int>(kSwitchUrlsMax); ++i) full.push_back("http://h" + std::to_string(i));
        const auto added = addServerUrl(full, "http://new", "http://h0");
        REQUIRE(added.size() == kSwitchUrlsMax);
        CHECK(added.front() == "http://h0");
        CHECK(added[1] == "http://h2");
        CHECK(added.back() == "http://new");
        CHECK(readServerFile(writeServerFile(added), JsonLimits{}) == added);
    }

    TEST_CASE("switch downloads: space: the limit, the 1 GB floor, and no write when the SD card can't say") {
        const std::uint64_t gb = kGiB;
        CHECK(checkSpace(0, 100, 8 * gb, 40 * gb, gb) == SpaceCheck::Fits);
        CHECK(checkSpace(8 * gb - 50, 100, 8 * gb, 40 * gb, gb) == SpaceCheck::LimitFull);
        CHECK(checkSpace(8 * gb - 100, 100, 8 * gb, 40 * gb, gb) == SpaceCheck::Fits);  // exactly full still fits
        CHECK(checkSpace(0, 100, 0, 40 * gb, gb) == SpaceCheck::Fits);                  // no limit
        CHECK(checkSpace(0, 100, 8 * gb, gb + 50, gb) == SpaceCheck::SdFull);
        CHECK(checkSpace(0, 100, 8 * gb, 50, gb) == SpaceCheck::SdFull);
        CHECK(checkSpace(0, 100, 8 * gb, std::nullopt, gb) == SpaceCheck::Unknown);
        CHECK(checkSpace(UINT64_MAX, 100, 8 * gb, 40 * gb, gb) == SpaceCheck::LimitFull);  // no overflow

        SwitchDownloadSettings settings;
        CHECK(switchLimitBytes(settings, 8 * gb) == 8 * gb);
        settings.limitGb = 2;
        CHECK(switchLimitBytes(settings, 8 * gb) == 2 * gb);
        settings.limitGb = 0;
        CHECK(switchLimitBytes(settings, 8 * gb) == 0);
        settings.limitGb = 3;
        CHECK(switchLimitBytes(settings, 8 * gb) == 8 * gb);
    }

    TEST_CASE("switch downloads: what the tab shows") {
        CHECK(switchQueueView(0, 0, false, SwitchWait::None) == SwitchQueueView::Idle);
        CHECK(switchQueueView(3, 0, false, SwitchWait::None) == SwitchQueueView::Running);
        CHECK(switchQueueView(3, 1, true, SwitchWait::None) == SwitchQueueView::Paused);
        CHECK(switchQueueView(3, 0, false, SwitchWait::LimitFull) == SwitchQueueView::Waiting);
        CHECK(switchQueueView(3, 0, true, SwitchWait::Server) == SwitchQueueView::Paused);  // the user's pause wins
        CHECK(switchQueueView(2, 2, false, SwitchWait::None) == SwitchQueueView::Stopped);
        CHECK(switchQueueView(2, 2, true, SwitchWait::Server) == SwitchQueueView::Stopped);
        CHECK(std::string(switchQueueViewName(SwitchQueueView::Waiting)) == "waiting");
        CHECK(std::string(switchWaitName(SwitchWait::Background)) == "background");
    }

    TEST_CASE("switch downloads: sizes in words") {
        CHECK(bytesText(0) == "0 B");
        CHECK(bytesText(512) == "512 B");
        CHECK(bytesText(1024) == "1 KB");
        CHECK(bytesText(12 * 1024) == "12 KB");
        CHECK(bytesText(static_cast<std::uint64_t>(1.8 * 1024 * 1024)) == "1.8 MB");
        CHECK(bytesText(340ULL * 1024 * 1024) == "340 MB");
        CHECK(bytesText(static_cast<std::uint64_t>(2.3 * 1024 * 1024 * 1024)) == "2.3 GB");
        CHECK(bytesText(8ULL * 1024 * 1024 * 1024) == "8 GB");
        CHECK(bytesText(static_cast<std::uint64_t>(9.97 * 1024 * 1024)) == "10 MB");
    }

    TEST_CASE("switch downloads: the settings section and the test config's keys") {
        AppConfig config;
        CHECK_FALSE(config.switchDownloads.limitGb);
        config.switchDownloads.limitGb = 16;
        config.switchDownloads.deleteAfterReading = true;
        const ConfigLoad load = loadAppConfig(saveAppConfig(config));
        CHECK(load.config.switchDownloads.limitGb == 16);
        CHECK(load.config.switchDownloads.deleteAfterReading);
        const ConfigLoad odd = loadAppConfig(R"({"schemaVersion":1,"switchDownloads":{"limitGb":7}})");
        CHECK_FALSE(odd.config.switchDownloads.limitGb);
        CHECK_FALSE(odd.problems.empty());
        const ConfigLoad none = loadAppConfig(R"({"schemaVersion":1,"switchDownloads":{"limitGb":0}})");
        CHECK(none.config.switchDownloads.limitGb == 0);

        const testconfig::TestConfig test =
            testconfig::parse(R"({"switchDownloadLimitBytes": 1500000, "sdFreeBytes": 1073841824})");
        CHECK(test.switchDownloadLimitBytes == 1500000u);
        CHECK(test.sdFreeBytes == 1073841824u);
        CHECK(test.problems.empty());
        CHECK_FALSE(testconfig::parse("{}").sdFreeBytes);
        CHECK_FALSE(testconfig::parse(R"({"sdFreeBytes": -1})").problems.empty());

        const RuntimeProfile full = makeRuntimeProfile(LaunchMode::Application, 4 * kGiB, kGiB);
        CHECK(full.switchDownloadPagesAtOnce == 2);
        CHECK(full.switchDownloadLimitBytes == 8 * kGiB);
        CHECK(makeRuntimeProfile(LaunchMode::Application, 4 * kGiB, kGiB, true).switchDownloadPagesAtOnce == 1);
    }
}
