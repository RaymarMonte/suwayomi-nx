// WP-17 (D58): downloads on the server, the pure rules (core/ServerDownloads.hpp) and their GraphQL answers.
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/AppConfig.hpp"
#include "core/ServerDownloads.hpp"
#include "core/graphql/Operations.hpp"

using namespace snx::core;

namespace {

nlohmann::json row(int chapterId, const char* state, double progress = 0, int tries = 0, int position = -1) {
    nlohmann::json r = {{"progress", progress},
                        {"state", state},
                        {"tries", tries},
                        {"chapter", {{"id", chapterId}, {"name", "Chapter " + std::to_string(chapterId)}, {"sourceOrder", chapterId}}},
                        {"manga", {{"id", 7}, {"title", "Paper Garden"}, {"thumbnailUrl", "/api/v1/manga/7/thumbnail"}}}};
    if (position >= 0) {
        r["position"] = position;
    }
    return r;
}

nlohmann::json status(const char* state, nlohmann::json rows) {
    return {{"state", state}, {"queue", std::move(rows)}};
}

Chapter chapter(int id, int sourceOrder, bool read = false, bool downloaded = false) {
    Chapter c;
    c.id = id;
    c.sourceOrder = sourceOrder;
    c.isRead = read;
    c.isDownloaded = downloaded;
    return c;
}

GqlResponse response(nlohmann::json data) {
    GqlResponse r;
    r.data = std::move(data);
    return r;
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("server downloads: a queue reads its rows in the server's order, with every field") {
        const auto queue = readDownloadQueue(status("STARTED", {row(12, "QUEUED", 0, 0, 1), row(11, "DOWNLOADING", 0.333, 0, 0)}),
                                             1000);
        REQUIRE(queue.ok());
        const DownloadQueue& q = queue.value();
        CHECK(q.started);
        REQUIRE(q.rows.size() == 2);
        CHECK(q.rows[0].chapterId == 11);  // position 0 first
        CHECK(q.rows[0].state == DownloadState::Downloading);
        CHECK(q.rows[0].progress == doctest::Approx(0.333));
        CHECK(q.rows[0].chapterName == "Chapter 11");
        CHECK(q.rows[0].mangaTitle == "Paper Garden");
        CHECK(q.rows[0].mangaId == 7);
        CHECK(q.rows[0].thumbnailUrl == "/api/v1/manga/7/thumbnail");
        CHECK(q.rows[1].chapterId == 12);
        CHECK(q.queued == 1);
        CHECK(q.downloading == 1);
        CHECK(q.failed == 0);
        CHECK(q.total() == 2);
        CHECK(q.waiting());
    }

    TEST_CASE("server downloads: broken rows are skipped and counted, FINISHED ones left out, odd values clamped") {
        nlohmann::json noId = row(1, "QUEUED");
        noId["chapter"].erase("id");
        nlohmann::json odd = row(5, "ERROR", 7.5, -2);
        odd.erase("manga");
        const auto queue = readDownloadQueue(
            status("STOPPED", {noId, row(2, "WEIRD"), "not a row", row(3, "FINISHED", 1), row(4, "QUEUED"), odd}), 1000);
        REQUIRE(queue.ok());
        const DownloadQueue& q = queue.value();
        CHECK_FALSE(q.started);
        CHECK(q.skipped == 3);
        REQUIRE(q.rows.size() == 2);
        CHECK(q.rows[0].chapterId == 4);
        CHECK(q.rows[1].chapterId == 5);
        CHECK(q.rows[1].progress == 1.0);
        CHECK(q.rows[1].tries == 0);
        CHECK(q.rows[1].mangaId == 0);
        CHECK(q.failed == 1);
        CHECK_FALSE(readDownloadQueue(nlohmann::json::array(), 10).ok());
        CHECK(readDownloadQueue(nlohmann::json::object(), 10).value().total() == 0);  // no queue: empty
    }

    TEST_CASE("server downloads: rows past the limit are only counted (S6), the counts cover them too") {
        nlohmann::json rows = nlohmann::json::array();
        for (int i = 0; i < 5; ++i) {
            rows.push_back(row(100 + i, i == 4 ? "ERROR" : "QUEUED", 0, 0, i));
        }
        const DownloadQueue q = readDownloadQueue(status("STARTED", rows), 3).value();
        CHECK(q.rows.size() == 3);
        CHECK(q.leftOut == 2);
        CHECK(q.total() == 5);
        CHECK(q.queued == 4);
        CHECK(q.failed == 1);
    }

    TEST_CASE("server downloads: the tab's view (Idle, only failed, Running, Starting soon after our start, else Paused)") {
        const DownloadQueue empty = readDownloadQueue(status("STARTED", nlohmann::json::array()), 10).value();
        const DownloadQueue failed = readDownloadQueue(status("STOPPED", {row(1, "ERROR", 0, 3)}), 10).value();
        const DownloadQueue running = readDownloadQueue(status("STARTED", {row(1, "DOWNLOADING")}), 10).value();
        const DownloadQueue stopped = readDownloadQueue(status("STOPPED", {row(1, "QUEUED")}), 10).value();
        CHECK(queueView(empty, 1000, 0, 5000) == QueueView::Idle);  // STARTED with nothing in it, as seen for 0.5 s
        CHECK(queueView(failed, 1000, 900, 5000) == QueueView::Idle);
        CHECK(queueView(running, 1000, 0, 5000) == QueueView::Running);
        CHECK(queueView(stopped, 1000, 0, 5000) == QueueView::Paused);
        CHECK(queueView(stopped, 5000, 1000, 5000) == QueueView::Starting);
        CHECK(queueView(stopped, 6000, 1000, 5000) == QueueView::Paused);  // the wait ran out
        CHECK(std::string(queueViewName(QueueView::Starting)) == "starting");
    }

    TEST_CASE("server downloads: percent is rounded down, so 100 % means done") {
        CHECK(percentText(0) == "0 %");
        CHECK(percentText(0.4) == "40 %");
        CHECK(percentText(0.29) == "29 %");
        CHECK(percentText(0.6666) == "66 %");
        CHECK(percentText(0.999) == "99 %");
        CHECK(percentText(1) == "100 %");
        CHECK(percentText(-1) == "0 %");
        CHECK(percentText(5) == "100 %");
    }

    TEST_CASE("server downloads: the bulk choices take reading order, skip downloaded and queued, Next N and Unread only unread") {
        // Newest first, as the details screen holds them.
        std::vector<Chapter> chapters;
        for (int order = 30; order >= 1; --order) {
            chapters.push_back(chapter(1000 + order, order, order <= 2, order == 3));
        }
        const std::vector<int> queued = {1004};
        CHECK(chaptersToDownload(chapters, DownloadChoice::Next1, queued) == std::vector<int>{1005});
        const std::vector<int> next5 = chaptersToDownload(chapters, DownloadChoice::Next5, queued);
        CHECK(next5 == std::vector<int>{1005, 1006, 1007, 1008, 1009});
        CHECK(chaptersToDownload(chapters, DownloadChoice::Next25, queued).size() == 25);
        CHECK(chaptersToDownload(chapters, DownloadChoice::Unread, queued).size() == 26);  // 30 - 2 read - 1 downloaded - 1 queued
        const std::vector<int> all = chaptersToDownload(chapters, DownloadChoice::All, queued);
        CHECK(all.size() == 28);
        CHECK(all.front() == 1001);  // read ones too
        std::vector<Chapter> done = {chapter(1, 1, true), chapter(2, 2, false, true)};
        CHECK(chaptersToDownload(done, DownloadChoice::Unread, {}).empty());
        CHECK(chaptersToDownload(done, DownloadChoice::All, {}) == std::vector<int>{1});
    }

    TEST_CASE("server downloads: ids go in batches, and rows that left are found") {
        CHECK(idBatches({1, 2, 3, 4, 5}, 2) == std::vector<std::vector<int>>{{1, 2}, {3, 4}, {5}});
        CHECK(idBatches({1, 2}, 0) == std::vector<std::vector<int>>{{1}, {2}});
        CHECK(idBatches({}, 10).empty());
        const DownloadQueue before = readDownloadQueue(status("STARTED", {row(1, "DOWNLOADING"), row(2, "QUEUED"), row(3, "QUEUED")}), 10).value();
        const DownloadQueue after = readDownloadQueue(status("STARTED", {row(2, "DOWNLOADING")}), 10).value();
        CHECK(chaptersThatLeft(before, after) == std::vector<int>{1, 3});
        const DownloadQueue cut = readDownloadQueue(status("STARTED", {row(2, "DOWNLOADING"), row(9, "QUEUED")}), 1).value();
        CHECK(chaptersThatLeft(before, cut).empty());  // rows left out: can't tell
    }

    TEST_CASE("server downloads: the documents, and the queue read from the query and from every mutation") {
        CHECK(gql::downloadStatus().name == "DownloadStatus");
        CHECK_FALSE(gql::downloadStatus().mutation);
        CHECK(gql::clearDownloader().mutation);
        CHECK(gql::enqueueChapterDownloads({4, 5}).variables["ids"] == nlohmann::json::array({4, 5}));
        CHECK(gql::dequeueChapterDownload(4).variables["id"] == 4);
        CHECK(gql::reorderChapterDownload(4, -3).variables["to"] == 0);
        CHECK(gql::deleteDownloadedChapters({8}).variables["ids"] == nlohmann::json::array({8}));
        for (const char* name : {"startDownloader", "stopDownloader", "clearDownloader", "enqueueChapterDownloads",
                                 "dequeueChapterDownload", "reorderChapterDownload"}) {
            CAPTURE(name);
            const auto q = gql::readDownloadAnswer(response({{name, {{"downloadStatus", status("STARTED", {row(3, "QUEUED")})}}}}), 10);
            REQUIRE(q.ok());
            CHECK(q.value().rows.size() == 1);
        }
        const auto query = gql::readDownloadAnswer(response({{"downloadStatus", status("STOPPED", nlohmann::json::array())}}), 10);
        REQUIRE(query.ok());
        CHECK(query.value().total() == 0);
        GqlResponse failed;
        failed.data = {{"startDownloader", nullptr}};
        failed.errors.push_back({"Timed out waiting for 30000 ms", {}});
        const auto timeout = gql::readDownloadAnswer(failed, 10);
        REQUIRE_FALSE(timeout.ok());
        CHECK(timeout.error().message.find("30000") != std::string::npos);
        CHECK_FALSE(gql::readDownloadAnswer(response(nlohmann::json::object()), 10).ok());

        const auto deleted = gql::readDeleteDownloadedChapters(response(
            {{"deleteDownloadedChapters", {{"chapters", {{{"id", 3}, {"isDownloaded", false}}, {{"isDownloaded", true}}}}}}}));
        REQUIRE(deleted.ok());
        REQUIRE(deleted.value().size() == 1);  // the one without an id is left out
        CHECK(deleted.value()[0].id == 3);
        CHECK_FALSE(deleted.value()[0].isDownloaded);
    }

    TEST_CASE("server downloads: config.json keeps Delete server downloads after reading (a new section, off by default)") {
        CHECK_FALSE(loadAppConfig("{}").config.serverDownloads.deleteAfterReading);
        AppConfig config;
        config.serverDownloads.deleteAfterReading = true;
        const ConfigLoad back = loadAppConfig(saveAppConfig(config));
        CHECK(back.problems.empty());
        CHECK(back.config.serverDownloads.deleteAfterReading);
        CHECK_FALSE(loadAppConfig(R"({"serverDownloads": {"deleteAfterReading": "yes"}})").problems.empty());
    }
}
