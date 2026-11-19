// WP-17 (D58): the server download queue's model (src/ui/downloads/ServerDownloads) on a FakeHttpClient that plays the
// server's queue the way the test server behaves (SUWAYOMI-API.md "Server downloads"), with a played clock and schedule.
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "ApiTestKit.hpp"
#include "api/SuwayomiClient.hpp"
#include "net/FakeHttpClient.hpp"
#include "ui/downloads/ServerDownloads.hpp"

using namespace snx;
using net::FakeHttpClient;
using net::Request;
using ui::ServerDownloads;

namespace {

// The server's queue. Like the real one: an enqueue answers STOPPED (it starts about a second later, start() here),
// a dequeue of a chapter that isn't queued and a start with nothing queued fail after a wait.
struct FakeQueue {
    struct Row {
        int id = 0;
        std::string state = "QUEUED";
        double progress = 0;
        int tries = 0;
    };
    bool started = false;
    std::vector<Row> rows;
    bool away = false;
    bool permission = false;  // multi-user: this user may not download (D38's Permission)
    std::vector<int> downloaded;

    nlohmann::json status() const {
        nlohmann::json queue = nlohmann::json::array();
        int position = 0;
        for (const Row& r : rows) {
            queue.push_back({{"position", position++},
                             {"progress", r.progress},
                             {"state", r.state},
                             {"tries", r.tries},
                             {"chapter", {{"id", r.id}, {"name", "Chapter " + std::to_string(r.id)}, {"sourceOrder", r.id}}},
                             {"manga", {{"id", 7}, {"title", "Paper Garden"}, {"thumbnailUrl", ""}}}});
        }
        return {{"state", started ? "STARTED" : "STOPPED"}, {"queue", queue}};
    }
    bool has(int id) const {
        return std::any_of(rows.begin(), rows.end(), [id](const Row& r) { return r.id == id; });
    }
    bool anyQueued() const {
        return std::any_of(rows.begin(), rows.end(), [](const Row& r) { return r.state == "QUEUED"; });
    }
    // The downloader takes its next step: the first row downloads, a full one leaves.
    void finishFirst() {
        if (!rows.empty()) {
            downloaded.push_back(rows.front().id);
            rows.erase(rows.begin());
        }
        if (rows.empty()) {
            started = false;
        }
    }

    static net::HttpResult wrapped(const char* field, nlohmann::json status) {
        nlohmann::json data;
        if (std::string(field) == "downloadStatus") {
            data[field] = std::move(status);
        } else {
            data[field] = {{"downloadStatus", std::move(status)}};
        }
        return FakeHttpClient::answer(200, nlohmann::json{{"data", data}}.dump());
    }
    static net::HttpResult failed(const char* field, const char* message) {
        return FakeHttpClient::answer(
            200, nlohmann::json{{"data", {{field, nullptr}}}, {"errors", {{{"message", message}}}}}.dump());
    }

    net::HttpResult answer(const Request& request) {
        if (away) {
            return FakeHttpClient::answer(503, "away");
        }
        const std::string op = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (op == "DownloadStatus") {
            return wrapped("downloadStatus", status());
        }
        if (op == "EnqueueChapterDownloads") {
            if (permission) {
                return failed("enqueueChapterDownloads", "Permission denied: DOWNLOAD_CHAPTERS");
            }
            for (int id : vars["ids"].get<std::vector<int>>()) {
                auto it = std::find_if(rows.begin(), rows.end(), [id](const Row& r) { return r.id == id; });
                if (it == rows.end()) {
                    rows.push_back({id});
                } else if (it->state == "ERROR") {
                    it->state = "QUEUED";  // retry: tries stay
                }
            }
            return wrapped("enqueueChapterDownloads", status());
        }
        if (op == "DequeueChapterDownload") {
            const int id = vars["id"].get<int>();
            if (!has(id)) {
                return failed("dequeueChapterDownload", "Timed out waiting for 30000 ms");
            }
            rows.erase(std::remove_if(rows.begin(), rows.end(), [id](const Row& r) { return r.id == id; }), rows.end());
            return wrapped("dequeueChapterDownload", status());
        }
        if (op == "ReorderChapterDownload") {
            const int id = vars["chapterId"].get<int>();
            const int to = vars["to"].get<int>();
            auto it = std::find_if(rows.begin(), rows.end(), [id](const Row& r) { return r.id == id; });
            Row moved = *it;
            rows.erase(it);
            rows.insert(rows.begin() + std::min<int>(to, static_cast<int>(rows.size())), moved);
            return wrapped("reorderChapterDownload", status());
        }
        if (op == "StartDownloader") {
            if (!anyQueued()) {
                return failed("startDownloader", "Timed out waiting for 30000 ms");
            }
            started = true;
            return wrapped("startDownloader", status());
        }
        if (op == "StopDownloader") {
            started = false;
            for (Row& r : rows) {
                if (r.state == "DOWNLOADING") {
                    r.state = "QUEUED";  // progress kept
                }
            }
            return wrapped("stopDownloader", status());
        }
        if (op == "ClearDownloader") {
            rows.clear();
            started = false;
            return wrapped("clearDownloader", status());
        }
        if (op == "DeleteDownloadedChapters") {
            nlohmann::json chapters = nlohmann::json::array();
            for (int id : vars["ids"].get<std::vector<int>>()) {
                chapters.push_back({{"id", id}, {"isDownloaded", false}});
            }
            return FakeHttpClient::answer(
                200, nlohmann::json{{"data", {{"deleteDownloadedChapters", {{"chapters", chapters}}}}}}.dump());
        }
        return FakeHttpClient::answer(404);
    }
};

struct Harness {
    FakeQueue server;
    FakeHttpClient http;
    std::unique_ptr<api::SuwayomiClient> client;
    std::unique_ptr<ServerDownloads> downloads;
    std::vector<ServerDownloads::Change> changes;
    std::int64_t now = 1000;
    std::vector<std::pair<int, std::function<void()>>> scheduled;

    explicit Harness(int batchMax = 1000) {
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = "http://192.168.1.10:4567";
        client = std::make_unique<api::SuwayomiClient>(http, settings, api::ClientOptions{});
        ServerDownloads::Limits limits;  // poll 1 s, start wait 5 s, retry 5 s .. 60 s
        limits.batchMax = batchMax;
        downloads = std::make_unique<ServerDownloads>(
            *client, limits, [this] { return now; },
            [this](int ms, std::function<void()> fn) { scheduled.emplace_back(ms, std::move(fn)); });
        downloads->addListener([this](const ServerDownloads::Change& change) { changes.push_back(change); });
    }

    int runNext() {  // the next scheduled task, the clock moved on by its delay; -1: nothing scheduled
        if (scheduled.empty()) {
            return -1;
        }
        auto [ms, fn] = std::move(scheduled.front());
        scheduled.erase(scheduled.begin());
        now += ms;
        fn();
        return ms;
    }
    int count(const std::string& op) const {
        int n = 0;
        for (const Request& r : http.requests()) {
            n += operationOf(r) == op ? 1 : 0;
        }
        return n;
    }
    std::vector<int> ids() const { return downloads->queuedIds(); }
};

}  // namespace

TEST_SUITE("ui") {
    TEST_CASE("server downloads: no requests until watched; an idle queue is read once and not polled") {
        Harness h;
        CHECK(h.http.requests().empty());
        const int watcher = h.downloads->watch();
        CHECK(h.count("DownloadStatus") == 1);
        CHECK(h.downloads->polls() == 1);
        CHECK(h.downloads->view() == core::QueueView::Idle);
        CHECK(h.scheduled.empty());  // nothing busy: no polling
        h.downloads->unwatch(watcher);
        CHECK_FALSE(h.downloads->watched());
    }

    TEST_CASE("server downloads: a running queue is polled while watched, and not after the last watcher left") {
        Harness h;
        h.server.rows = {{11, "DOWNLOADING", 0.3}, {12}};
        h.server.started = true;
        const int watcher = h.downloads->watch();
        CHECK(h.downloads->view() == core::QueueView::Running);
        CHECK(h.runNext() == 1000);
        CHECK(h.count("DownloadStatus") == 2);
        h.server.finishFirst();
        CHECK(h.runNext() == 1000);
        REQUIRE(!h.changes.empty());
        CHECK(h.changes.back().left == std::vector<int>{11});  // it left by itself: most likely downloaded
        h.downloads->unwatch(watcher);
        CHECK(h.runNext() == 1000);  // the poll already scheduled finds nobody watching
        CHECK(h.count("DownloadStatus") == 3);
        CHECK(h.scheduled.empty());
    }

    TEST_CASE("server downloads: the enqueue's STOPPED answer shows Starting, then Running; Paused once the wait ran out") {
        Harness h;
        h.downloads->watch();
        REQUIRE(h.downloads->enqueue({21, 22}));
        CHECK(h.ids() == std::vector<int>{21, 22});
        CHECK(h.downloads->view() == core::QueueView::Starting);
        REQUIRE(h.changes.back().action);
        CHECK(h.changes.back().action->action == ServerDownloads::Action::Enqueue);
        CHECK(h.changes.back().action->chapters == 2);
        h.server.started = true;  // the server begins by itself
        h.runNext();
        CHECK(h.downloads->view() == core::QueueView::Running);
        // Another queue that never starts (the user paused it elsewhere): Paused after 5 s, polling stops.
        Harness p;
        p.downloads->watch();
        p.downloads->enqueue({5});
        for (int i = 0; i < 4; ++i) {
            CHECK(p.runNext() == 1000);
            CHECK(p.downloads->view() == core::QueueView::Starting);
        }
        CHECK(p.runNext() == 1000);
        CHECK(p.downloads->view() == core::QueueView::Paused);
        CHECK(p.scheduled.empty());
    }

    TEST_CASE("server downloads: an older answer never replaces a newer one") {
        Harness h;
        h.server.rows = {{1, "DOWNLOADING"}, {2}, {3}};
        h.server.started = true;
        h.downloads->watch();
        h.http.hold(true);
        h.runNext();  // a poll (Prefetch), held
        REQUIRE(h.downloads->moveToTop(3));  // Interactive: released first
        REQUIRE(h.http.releaseNext());
        CHECK(h.ids() == std::vector<int>{3, 1, 2});
        h.server.rows.push_back({4});  // the late poll's answer would show this row
        REQUIRE(h.http.releaseNext());
        h.http.hold(false);
        CHECK(h.ids() == std::vector<int>{3, 1, 2});  // dropped
    }

    TEST_CASE("server downloads: remove only what is listed; a failed remove of a chapter that is gone stays quiet") {
        Harness h;
        h.server.rows = {{1, "DOWNLOADING"}, {2}};
        h.server.started = true;
        h.downloads->watch();
        CHECK_FALSE(h.downloads->removeFromQueue(99));  // not in the queue: nothing sent (the server would hang 30 s)
        CHECK(h.count("DequeueChapterDownload") == 0);
        REQUIRE(h.downloads->removeFromQueue(2));
        CHECK(h.ids() == std::vector<int>{1});
        CHECK(h.changes.back().left.empty());  // our own remove isn't "left"
        // Chapter 1 finishes while its remove is on its way: the server fails, the check read finds it gone.
        h.http.hold(true);
        REQUIRE(h.downloads->removeFromQueue(1));
        h.server.finishFirst();
        REQUIRE(h.http.releaseNext());  // the dequeue fails
        REQUIRE(h.http.releaseNext());  // the check read
        h.http.hold(false);
        REQUIRE(h.changes.back().action);
        CHECK(h.changes.back().action->ok);
        CHECK_FALSE(h.downloads->takeError());
        CHECK(h.ids().empty());
    }

    TEST_CASE("server downloads: pause and resume; resume only with a QUEUED row, retry = enqueue again") {
        Harness h;
        h.server.rows = {{1, "DOWNLOADING", 0.66}, {2, "ERROR", 0, 3}};
        h.server.started = true;
        h.downloads->watch();
        REQUIRE(h.downloads->pause());
        CHECK(h.downloads->view() == core::QueueView::Paused);
        CHECK(h.downloads->queue()->rows[0].progress == doctest::Approx(0.66));  // kept
        CHECK_FALSE(h.downloads->pause());
        REQUIRE(h.downloads->resume());
        CHECK(h.downloads->view() == core::QueueView::Running);
        CHECK_FALSE(h.downloads->resume());  // already running
        h.server.finishFirst();
        h.server.started = false;  // only the failed row is left: the server stops
        h.runNext();
        CHECK(h.downloads->view() == core::QueueView::Idle);
        CHECK(h.downloads->queue()->failed == 1);
        CHECK_FALSE(h.downloads->resume());  // nothing QUEUED: start would hang 30 s
        CHECK(h.count("StartDownloader") == 1);
        CHECK_FALSE(h.downloads->retry(99));
        REQUIRE(h.downloads->retryFailed());
        CHECK(h.count("EnqueueChapterDownloads") == 1);
        CHECK(h.downloads->row(2)->state == core::DownloadState::Queued);
        CHECK(h.downloads->row(2)->tries == 3);
        CHECK(h.downloads->view() == core::QueueView::Starting);
    }

    TEST_CASE("server downloads: move to bottom, clear, delete, the batch limit, and errors through takeError") {
        Harness h(2);
        h.downloads->watch();
        REQUIRE(h.downloads->enqueue({1, 2, 3, 4, 5}));
        CHECK(h.count("EnqueueChapterDownloads") == 3);  // 2 + 2 + 1
        CHECK(h.ids() == std::vector<int>{1, 2, 3, 4, 5});
        REQUIRE(h.downloads->moveToBottom(1));
        CHECK(h.ids() == std::vector<int>{2, 3, 4, 5, 1});
        REQUIRE(h.downloads->clear());
        CHECK(h.ids().empty());
        CHECK(h.changes.back().left.empty());  // cleared by us
        CHECK_FALSE(h.downloads->clear());
        REQUIRE(h.downloads->deleteDownloads({8, 9}));
        REQUIRE(h.changes.back().deleted.size() == 2);
        CHECK_FALSE(h.changes.back().deleted[0].isDownloaded);
        CHECK(h.changes.back().action->action == ServerDownloads::Action::Delete);
        h.server.permission = true;
        REQUIRE(h.downloads->enqueue({6}));
        const auto error = h.downloads->takeError();
        REQUIRE(error);
        CHECK(error->kind == api::ApiErrorKind::Permission);
        CHECK_FALSE(h.downloads->takeError());
    }

    TEST_CASE("server downloads: a failed poll says so and backs off, then recovers") {
        Harness h;
        h.server.rows = {{1, "DOWNLOADING"}};
        h.server.started = true;
        h.downloads->watch();
        h.server.away = true;
        CHECK(h.runNext() == 1000);
        CHECK(h.downloads->pollFailed());
        CHECK(h.runNext() == 5000);
        CHECK(h.runNext() == 10000);
        h.server.away = false;
        CHECK(h.runNext() == 20000);
        CHECK_FALSE(h.downloads->pollFailed());
        CHECK(h.runNext() == 1000);  // back to the normal pace
    }

    TEST_CASE("server downloads: reset forgets the queue and reads again; destroyed while a request is out") {
        Harness h;
        h.server.rows = {{1, "DOWNLOADING"}};
        h.server.started = true;
        h.downloads->watch();
        h.http.hold(true);
        h.downloads->refresh();  // nothing: a read? no, the first one is done, so this one is held
        h.downloads->reset();
        CHECK_FALSE(h.downloads->queue());  // the new read is held too
        h.http.releaseAll();
        h.http.hold(false);
        REQUIRE(h.downloads->queue());
        CHECK(h.ids() == std::vector<int>{1});
        const int scheduledBefore = static_cast<int>(h.scheduled.size());
        CHECK(scheduledBefore >= 1);
        h.http.hold(true);
        h.downloads->refresh();
        h.downloads.reset();  // gone while its read waits
        h.http.releaseAll();
        while (h.runNext() >= 0) {
        }
        CHECK(h.changes.size() >= 1);
    }
}
