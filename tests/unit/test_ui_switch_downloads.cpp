// WP-18 (D59): downloads to the Switch's SD card. The file store (src/ui/downloads/DownloadStore) in a temp folder, and the
// queue model (src/ui/downloads/SwitchDownloads) on a FakeHttpClient that plays the server's page lists, pages and cover,
// with a played clock and schedule. The test thread plays the main thread (MainQueue): the client's answers, the store's
// answers and the model's posts all arrive there.
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "ApiTestKit.hpp"
#include "Fixtures.hpp"
#include "MainQueue.hpp"
#include "api/SuwayomiClient.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/SwitchDownloads.hpp"
#include "net/FakeHttpClient.hpp"
#include "platform/SafeFs.hpp"
#include "ui/downloads/DownloadStore.hpp"
#include "ui/downloads/SwitchDownloads.hpp"

using namespace snx;
using net::FakeHttpClient;
using net::Request;
using ui::DownloadStore;
using ui::SwitchDownloads;
using State = ui::SwitchDownloads::ChapterState;

namespace {

constexpr const char* kUrl = "http://s:4567";
constexpr int kMangaId = 7;

// A fresh folder for a SafeFs, removed at the end.
struct TempRoot {
    std::string parent;
    std::string root;
    TempRoot() {
        char pattern[] = "/tmp/snx-switchdl-XXXXXX";
        const char* made = ::mkdtemp(pattern);
        REQUIRE(made != nullptr);
        parent = std::string(made) + "/";
        root = parent + "data/";
        REQUIRE(::mkdir(root.c_str(), 0755) == 0);
    }
    ~TempRoot() {
        platform::SafeFs(parent).removeTree("data");
        ::rmdir(parent.c_str());
    }
};

std::string pageUrl(int sourceOrder, int index) {
    return "/api/v1/manga/7/chapter/" + std::to_string(sourceOrder) + "/page/" + std::to_string(index);
}
const std::string kThumb = "/api/v1/manga/7/thumbnail";

// The server: chapters with their pages (fixture bytes), the manga's details and cover. `broken` answers a URL with an HTTP
// status (0 = no answer at all, a network error) `times` more times (-1 = always).
struct FakeServer {
    struct Chap {
        int sourceOrder = 0;
        std::vector<std::string> pages;  // the bytes of each page; "" = a page listed without a usable URL
    };
    std::map<int, Chap> chapters;
    std::string cover = readFixture("images/alpha-p1.jpg");
    std::string author = "Ann";
    struct Broken {
        int status = 500;
        int times = -1;
    };
    std::map<std::string, Broken> broken;
    std::map<std::string, int> gets;  // full URL -> how often asked
    int pageLists = 0;
    int mangaAsked = 0;

    net::HttpResult answer(const Request& request) {
        if (request.method == net::Method::Post) {
            const std::string op = operationOf(request);
            const nlohmann::json vars = variablesOf(request);
            if (op == "FetchChapterPages") {
                pageLists += 1;
                const int id = vars["chapterId"].get<int>();
                const auto found = chapters.find(id);
                if (found == chapters.end()) {
                    return FakeHttpClient::answer(
                        200, nlohmann::json{{"data", {{"fetchChapterPages", nullptr}}},
                                            {"errors", {{{"message", "No chapter found with id " + std::to_string(id)}}}}}
                                 .dump());
                }
                nlohmann::json pages = nlohmann::json::array();
                for (std::size_t i = 0; i < found->second.pages.size(); ++i) {
                    pages.push_back(found->second.pages[i].empty() ? std::string()
                                                                   : pageUrl(found->second.sourceOrder, static_cast<int>(i)));
                }
                return FakeHttpClient::answer(
                    200, nlohmann::json{{"data",
                                         {{"fetchChapterPages",
                                           {{"pages", pages},
                                            {"chapter",
                                             {{"id", id},
                                              {"mangaId", kMangaId},
                                              {"sourceOrder", found->second.sourceOrder},
                                              {"isRead", false},
                                              {"lastPageRead", 0},
                                              {"pageCount", pages.size()}}},
                                            {"syncConflict", nullptr}}}}}}
                             .dump());
            }
            if (op == "Manga") {
                mangaAsked += 1;
                return FakeHttpClient::answer(
                    200, nlohmann::json{{"data",
                                         {{"manga",
                                           {{"id", kMangaId},
                                            {"title", "Paper Garden"},
                                            {"thumbnailUrl", kThumb},
                                            {"author", author},
                                            {"artist", ""},
                                            {"description", "Leaves."},
                                            {"genre", {"Drama"}},
                                            {"status", "ONGOING"},
                                            {"inLibrary", true},
                                            {"initialized", true},
                                            {"unreadCount", 0},
                                            {"downloadCount", 0},
                                            {"source", {{"id", "1"}, {"name", "Local"}, {"displayName", "Local source"}}},
                                            {"categories", {{"nodes", nlohmann::json::array()}}},
                                            {"firstUnreadChapter", nullptr}}}}}}
                             .dump());
            }
            return FakeHttpClient::answer(404);
        }
        gets[request.url] += 1;
        if (auto b = broken.find(request.url); b != broken.end() && b->second.times != 0) {
            if (b->second.times > 0) {
                b->second.times -= 1;
            }
            if (b->second.status == 0) {
                return net::HttpResult::fail(core::ErrorKind::Network, "Couldn't connect to server");
            }
            return FakeHttpClient::answer(b->second.status, "nope");
        }
        if (request.url == std::string(kUrl) + kThumb) {
            return FakeHttpClient::answer(200, cover);
        }
        for (const auto& [id, chap] : chapters) {
            for (std::size_t i = 0; i < chap.pages.size(); ++i) {
                if (request.url == std::string(kUrl) + pageUrl(chap.sourceOrder, static_cast<int>(i))) {
                    return FakeHttpClient::answer(200, chap.pages[i]);
                }
            }
        }
        return FakeHttpClient::answer(404);
    }
    int got(int sourceOrder, int index) const {
        const auto found = gets.find(std::string(kUrl) + pageUrl(sourceOrder, index));
        return found == gets.end() ? 0 : found->second;
    }
    int gotCover() const {
        const auto found = gets.find(std::string(kUrl) + kThumb);
        return found == gets.end() ? 0 : found->second;
    }
};

const std::string& jpg() {
    static const std::string bytes = readFixture("images/alpha-p1.jpg");
    return bytes;
}
const std::string& png() {
    static const std::string bytes = readFixture("images/beta-p1.png");
    return bytes;
}
const std::string& webp() {
    static const std::string bytes = readFixture("images/gamma-strip.webp");
    return bytes;
}
const std::string& gif() {
    static const std::string bytes = readFixture("images/small.gif");
    return bytes;
}

core::Manga paperGarden() {
    core::Manga manga;
    manga.id = kMangaId;
    manga.title = "Paper Garden";
    manga.thumbnailUrl = kThumb;
    manga.author = "Ann";
    manga.genre = {"Drama"};
    return manga;
}

core::Chapter chapter(int id, int sourceOrder) {
    core::Chapter c;
    c.id = id;
    c.mangaId = kMangaId;
    c.name = "Chapter " + std::to_string(sourceOrder);
    c.chapterNumber = sourceOrder;
    c.sourceOrder = sourceOrder;
    return c;
}

struct Harness {
    TempRoot tmp;
    platform::SafeFs fs;
    FakeServer server;
    FakeHttpClient http;
    MainQueue main;
    std::unique_ptr<api::SuwayomiClient> client;
    std::unique_ptr<DownloadStore> store;
    std::unique_ptr<SwitchDownloads> model;
    std::atomic<long long> freeBytes{100LL * static_cast<long long>(core::kGiB)};  // -1 = the SD card can't say
    bool memoryLow = false;
    std::int64_t now = 1000;
    std::vector<std::pair<int, std::function<void()>>> scheduled;
    std::vector<int> delays;  // every later() asked, in order
    std::vector<SwitchDownloads::Change> changes;
    SwitchDownloads::Limits limits;

    Harness() : fs(tmp.root) {
        server.chapters[101] = {1, {jpg(), png(), webp()}};
        server.chapters[102] = {2, {jpg(), jpg()}};
        server.chapters[103] = {3, {gif(), jpg()}};
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = kUrl;
        api::ClientOptions options;
        options.deliver = main.deliver();
        client = std::make_unique<api::SuwayomiClient>(http, settings, options);
        start();
    }
    ~Harness() {
        if (model) {
            model->shutdown();
        }
        model.reset();
        store.reset();
    }

    void start(bool waitLoaded = true) {
        DownloadStore::Options options;
        options.freeBytes = [this]() -> std::optional<std::uint64_t> {
            const long long bytes = freeBytes.load();
            if (bytes < 0) {
                return std::nullopt;
            }
            return static_cast<std::uint64_t>(bytes);
        };
        store = std::make_unique<DownloadStore>(fs, options, main.deliver());
        SwitchDownloads::Hooks hooks;
        hooks.clockMs = [this] { return now; };
        hooks.later = [this](int ms, std::function<void()> fn) {
            delays.push_back(ms);
            scheduled.emplace_back(ms, std::move(fn));
        };
        hooks.post = main.deliver();
        hooks.nowSeconds = [] { return std::int64_t{1700000000}; };
        hooks.memoryLow = [this] { return memoryLow; };
        model = std::make_unique<SwitchDownloads>(*client, http, *store, limits, hooks);
        model->addListener([this](const SwitchDownloads::Change& change) { changes.push_back(change); });
        model->setServer(kUrl);
        if (waitLoaded) {
            REQUIRE(drive([this] { return model->loaded(); }));
        }
    }
    // The app closes and starts again on the same SD card.
    void restart() {
        model->shutdown();
        model.reset();
        store.reset();
        scheduled.clear();
        changes.clear();
        start();
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
    // Runs what the threads deliver and, once they are quiet, the next timer, until `done`.
    bool drive(const std::function<bool()>& done, int maxSteps = 400) {
        for (int i = 0; i < maxSteps; ++i) {
            main.settle(15);
            if (done()) {
                return true;
            }
            if (store && store->waiting() > 0) {
                continue;
            }
            if (scheduled.empty()) {
                return main.runUntil(done);
            }
            runNext();
        }
        return done();
    }
    // With http.hold(true): releases one request at a time until `done`.
    bool releaseUntil(const std::function<bool()>& done, int maxSteps = 200) {
        for (int i = 0; i < maxSteps; ++i) {
            main.settle(15);
            if (done()) {
                return true;
            }
            if (store && store->waiting() > 0) {
                continue;
            }
            if (!http.releaseNext() && runNext() < 0) {
                return done();
            }
        }
        return done();
    }
    // Waits until the store's file thread finished everything asked so far (it runs its tasks in order).
    void filesDone() {
        bool done = false;
        store->freeBytes([&done](std::optional<std::uint64_t>, int) { done = true; });
        REQUIRE(main.runUntil([&done] { return done; }));
    }
    bool downloaded(int chapterId) {
        return drive([this, chapterId] { return model->chapterState(chapterId).state == State::OnSwitch; });
    }

    std::string key() const { return core::switchServerKey(kUrl); }
    std::string mangaDir() const { return core::switchMangaDir(key(), kMangaId); }
    std::string chapterDir(int chapterId) const { return core::switchChapterDir(key(), kMangaId, chapterId); }
    std::string page(int chapterId, int index, char format) const {
        return chapterDir(chapterId) + "/" + core::switchPageName(index, format);
    }
    core::MangaFileLoad mangaFile() const {
        const auto text = fs.readFile(mangaDir() + "/" + core::kSwitchMangaFile, 1024 * 1024);
        REQUIRE(text.has_value());
        return core::readMangaFile(*text, {});
    }
    core::QueueFileLoad queueFile() const {
        const auto text = fs.readFile(core::switchServerDir(key()) + "/" + core::kSwitchQueueFile, 1024 * 1024);
        REQUIRE(text.has_value());
        return core::readQueueFile(*text, {}, 1000);
    }
    std::vector<int> queued() const {
        std::vector<int> ids;
        for (const core::SwitchQueueEntry& e : model->queue().entries) {
            ids.push_back(e.chapter.id);
        }
        return ids;
    }
    bool finished(int chapterId, bool ok) const {
        for (const auto& c : changes) {
            if (c.finished && c.finished->chapterId == chapterId && c.finished->ok == ok) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace

TEST_SUITE("ui") {
    TEST_CASE("switch downloads: a chapter downloads (cover, page list, pages) and manga.json commits it") {
        Harness h;
        CHECK(h.model->view() == core::SwitchQueueView::Idle);
        CHECK(h.http.requests().empty());
        h.model->connected();
        CHECK(h.model->enqueue(paperGarden(), {chapter(101, 1)}) == 1);
        REQUIRE(h.downloaded(101));

        // The order on the wire: the cover once, the page list, then the pages.
        const auto& requests = h.http.requests();
        REQUIRE(requests.size() == 5);
        CHECK(requests[0].url == std::string(kUrl) + kThumb);
        CHECK(operationOf(requests[1]) == "FetchChapterPages");
        CHECK(requests[2].url == std::string(kUrl) + pageUrl(1, 0));
        CHECK(h.server.mangaAsked == 0);  // the details came with the enqueue

        // The files: server.json first, the cover, one file per page in its own format, manga.json.
        CHECK(h.fs.exists(core::switchServerDir(h.key()) + "/" + core::kSwitchServerFile));
        CHECK(h.fs.readFile(h.mangaDir() + "/" + core::kSwitchCoverFile, 1 << 20) == jpg());
        CHECK(h.fs.readFile(h.page(101, 0, core::kPageJpeg), 1 << 20) == jpg());
        CHECK(h.fs.readFile(h.page(101, 1, core::kPagePng), 1 << 20) == png());
        CHECK(h.fs.readFile(h.page(101, 2, core::kPageWebp), 1 << 20) == webp());
        const core::MangaFileLoad index = h.mangaFile();
        REQUIRE(index.usable);
        CHECK(index.manga.title == "Paper Garden");
        CHECK(index.manga.coverBytes == jpg().size());
        REQUIRE(index.manga.chapters.size() == 1);
        CHECK(index.manga.chapters[0].formats == "jpw");
        CHECK(index.manga.chapters[0].bytes == jpg().size() + png().size() + webp().size());
        CHECK(index.manga.chapters[0].downloadedAt == 1700000000);

        // What the screens read.
        const auto status = h.model->chapterState(101);
        CHECK(status.pages == 3);
        CHECK(h.model->queue().entries.empty());
        CHECK(h.model->view() == core::SwitchQueueView::Idle);
        const auto files = h.model->pageFiles(101);
        REQUIRE(files.has_value());
        REQUIRE(files->size() == 3);
        CHECK((*files)[1] == h.page(101, 1, core::kPagePng));
        CHECK_FALSE(h.model->pageFiles(102).has_value());
        const auto rows = h.model->downloadedManga();
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].chapters == 1);
        CHECK(rows[0].coverFile == h.mangaDir() + "/" + core::kSwitchCoverFile);
        CHECK(h.model->space().used == jpg().size() * 2 + png().size() + webp().size());
        CHECK(h.finished(101, true));
        CHECK(h.model->skipIds().count(101) == 1);
        CHECK(h.model->enqueue(paperGarden(), {chapter(101, 1)}) == 0);  // already on the Switch

        // A second chapter of the same manga: no cover again.
        CHECK(h.model->enqueue(paperGarden(), {chapter(102, 2)}) == 1);
        REQUIRE(h.downloaded(102));
        CHECK(h.server.gotCover() == 1);
        CHECK(h.mangaFile().manga.chapters.size() == 2);
    }

    TEST_CASE("switch downloads: a page that can't be fetched waits for the server; after a restart only the missing pages come") {
        Harness h;
        h.server.broken[std::string(kUrl) + pageUrl(1, 2)] = {0, -1};  // no answer at all
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(101, 1)});
        REQUIRE(h.drive([&] { return h.model->wait() == core::SwitchWait::Server; }));
        CHECK(h.model->failedCount() == 0);  // no try counted
        CHECK(h.model->view() == core::SwitchQueueView::Waiting);
        REQUIRE(h.drive([&] { return h.fs.exists(h.page(101, 1, core::kPagePng)); }));
        CHECK_FALSE(h.fs.exists(h.page(101, 2, core::kPageWebp)));

        h.restart();  // the queue (queue.json, flushed at exit) and the pages so far are on the SD card
        CHECK(h.queued() == std::vector<int>{101});
        CHECK(h.model->space().used == jpg().size() * 2 + png().size());  // the cover and the two pages
        h.server.broken.clear();
        h.model->connected();
        REQUIRE(h.downloaded(101));
        CHECK(h.server.got(1, 0) == 1);
        CHECK(h.server.got(1, 1) == 1);
        CHECK(h.server.got(1, 2) >= 2);
        CHECK(h.server.mangaAsked == 1);  // after a restart: its details once, for manga.json
        CHECK(h.mangaFile().manga.author == "Ann");
    }

    TEST_CASE("switch downloads: a page that keeps failing fails its chapter at that page; the queue goes on; Retry fetches what's missing") {
        Harness h;
        h.server.broken[std::string(kUrl) + pageUrl(1, 1)] = {500, -1};
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(101, 1), chapter(102, 2)});
        REQUIRE(h.downloaded(102));
        const auto failed = h.model->chapterState(101);
        CHECK(failed.state == State::Failed);
        CHECK(failed.failedPage == 1);
        CHECK(h.server.got(1, 1) == 3);  // pageTries
        CHECK(h.model->queue().entries[0].error.find("500") != std::string::npos);
        CHECK(h.finished(101, false));
        CHECK(h.model->view() == core::SwitchQueueView::Stopped);
        h.store->flush();
        CHECK(h.queueFile().queue.entries.size() == 1);

        h.server.broken.clear();
        CHECK(h.model->retry(101));
        REQUIRE(h.downloaded(101));
        CHECK(h.server.got(1, 0) == 1);  // kept from before
        CHECK(h.server.got(1, 1) == 4);
        CHECK(h.model->failedCount() == 0);
        CHECK(h.model->view() == core::SwitchQueueView::Idle);
    }

    TEST_CASE("switch downloads: what isn't an image is never written; a 401 is tried once more; two in a row wait for a login") {
        Harness h;
        h.server.chapters[104] = {4, {"<html>not a page</html>", jpg()}};
        h.server.broken[std::string(kUrl) + pageUrl(2, 0)] = {401, 1};  // ui_login renewing its token
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(104, 4), chapter(102, 2)});
        REQUIRE(h.downloaded(102));
        CHECK(h.model->chapterState(104).state == State::Failed);
        CHECK(h.model->chapterState(104).failedPage == 0);
        CHECK_FALSE(h.fs.exists(h.page(104, 0, core::kPageJpeg)));
        CHECK(h.server.got(2, 0) == 2);

        h.server.chapters[103] = {3, {gif(), jpg()}};
        h.server.broken[std::string(kUrl) + pageUrl(3, 0)] = {401, -1};
        h.model->enqueue(paperGarden(), {chapter(103, 3)});
        REQUIRE(h.drive([&] { return h.model->wait() == core::SwitchWait::Login; }));
        CHECK(h.model->chapterState(103).state == State::Queued);  // not failed: it goes on after a login
        h.server.broken.clear();
        h.model->connected();
        REQUIRE(h.downloaded(103));
    }

    TEST_CASE("switch downloads: the limit and the SD card's free space hold the queue; a higher limit or a delete lets it go on") {
        Harness h;
        h.model->connected();
        const std::uint64_t first = jpg().size() * 2 + png().size() + webp().size();  // chapter 101 and the cover
        h.model->setLimit(first + jpg().size() / 2);
        h.model->enqueue(paperGarden(), {chapter(101, 1), chapter(102, 2)});
        REQUIRE(h.downloaded(101));
        REQUIRE(h.drive([&] { return h.model->wait() == core::SwitchWait::LimitFull; }));
        CHECK(h.model->view() == core::SwitchQueueView::Waiting);
        CHECK_FALSE(h.fs.exists(h.page(102, 0, core::kPageJpeg)));
        h.model->setLimit(0);  // no limit
        REQUIRE(h.downloaded(102));

        // The SD card: minSdFree always stays free.
        h.freeBytes = static_cast<long long>(h.limits.minSdFree + gif().size() + 100);
        h.model->enqueue(paperGarden(), {chapter(103, 3)});
        REQUIRE(h.drive([&] { return h.model->wait() == core::SwitchWait::SdFull; }));
        h.filesDone();
        CHECK(h.fs.exists(h.page(103, 0, core::kPageGif)));
        CHECK_FALSE(h.fs.exists(h.page(103, 1, core::kPageJpeg)));
        h.freeBytes = static_cast<long long>(100 * core::kGiB);
        CHECK(h.model->deleteChapters({101}));  // frees space: the free bytes are asked again and the queue goes on
        REQUIRE(h.downloaded(103));
        CHECK_FALSE(h.fs.exists(h.chapterDir(101)));
        CHECK(h.mangaFile().manga.chapters.size() == 2);
    }

    TEST_CASE("switch downloads: free space unknown writes nothing; it is asked again later") {
        Harness h;
        h.freeBytes = -1;
        h.restart();
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(102, 2)});
        REQUIRE(h.drive([&] { return h.model->wait() == core::SwitchWait::SpaceUnknown; }));
        CHECK_FALSE(h.fs.exists(h.page(102, 0, core::kPageJpeg)));
        h.freeBytes = static_cast<long long>(100 * core::kGiB);
        REQUIRE(h.downloaded(102));
    }

    TEST_CASE("switch downloads: pause keeps the pages and resume goes on from there") {
        Harness h;
        h.http.hold(true);
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(101, 1), chapter(102, 2)});
        REQUIRE(h.releaseUntil([&] { return h.fs.exists(h.page(101, 1, core::kPagePng)); }));
        CHECK(h.model->chapterState(101).state == State::Downloading);
        CHECK(h.model->pause());
        CHECK(h.model->view() == core::SwitchQueueView::Paused);
        const auto paused = h.model->chapterState(101);
        CHECK(paused.state == State::Paused);
        CHECK(paused.pagesDone == 2);
        CHECK(paused.pages == 3);
        h.http.releaseAll();  // cancelled
        h.main.settle(30);
        CHECK_FALSE(h.fs.exists(h.page(101, 2, core::kPageWebp)));
        h.http.hold(false);
        CHECK(h.model->resume());
        REQUIRE(h.downloaded(102));
        CHECK(h.model->chapterState(101).state == State::OnSwitch);
        CHECK(h.server.got(1, 0) == 1);
        CHECK(h.server.got(1, 1) == 1);
    }

    TEST_CASE("switch downloads: Top, Bottom, Remove and Clear; downloaded chapters stay") {
        Harness h;  // never connected: nothing downloads
        h.model->enqueue(paperGarden(), {chapter(101, 1), chapter(102, 2), chapter(103, 3)});
        CHECK(h.queued() == std::vector<int>{101, 102, 103});
        CHECK(h.model->wait() == core::SwitchWait::Server);
        CHECK(h.model->moveToTop(103));
        CHECK(h.queued() == std::vector<int>{103, 101, 102});
        CHECK(h.model->moveToBottom(103));
        CHECK(h.queued() == std::vector<int>{101, 102, 103});
        REQUIRE(h.fs.makeDirs(h.chapterDir(102)));
        REQUIRE(h.fs.writeAtomic(h.page(102, 0, core::kPageJpeg), jpg()));  // a page so far
        CHECK(h.model->removeFromQueue(102));
        CHECK(h.queued() == std::vector<int>{101, 103});
        h.filesDone();
        CHECK_FALSE(h.fs.exists(h.chapterDir(102)));
        CHECK(h.model->clear());
        CHECK(h.model->queue().entries.empty());
        CHECK(h.model->view() == core::SwitchQueueView::Idle);
        h.filesDone();
        CHECK_FALSE(h.fs.exists(h.mangaDir()));  // nothing of it is on the Switch or queued

        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(101, 1)});
        REQUIRE(h.downloaded(101));
        h.model->enqueue(paperGarden(), {chapter(102, 2)});
        h.model->pause();
        CHECK(h.model->clear());
        CHECK(h.model->chapterState(101).state == State::OnSwitch);
        CHECK(h.fs.exists(h.page(101, 0, core::kPageJpeg)));
    }

    TEST_CASE("switch downloads: HOME stops the requests and the writes and saves the queue at once; back in focus it goes on") {
        Harness h;
        h.http.hold(true);
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(101, 1)});
        REQUIRE(h.releaseUntil([&] { return h.fs.exists(h.page(101, 0, core::kPageJpeg)); }));
        h.model->setFocused(false);
        CHECK(h.model->wait() == core::SwitchWait::Background);
        CHECK_FALSE(h.store->writesAllowed());
        CHECK(h.queueFile().queue.entries.size() == 1);  // flushed, no timer needed
        h.http.releaseAll();
        h.main.settle(30);
        CHECK_FALSE(h.fs.exists(h.page(101, 2, core::kPageWebp)));
        h.http.hold(false);
        h.model->setFocused(true);
        REQUIRE(h.downloaded(101));
    }

    TEST_CASE("switch downloads: queue.json is saved at most once per delay and survives a restart; each server has its own folder") {
        Harness h;
        h.model->enqueue(paperGarden(), {chapter(101, 1)});
        h.model->enqueue(paperGarden(), {chapter(102, 2)});
        h.model->enqueue(paperGarden(), {chapter(103, 3)});
        CHECK(h.model->queueWrites() == 0);
        REQUIRE(h.scheduled.size() == 1);
        CHECK(h.scheduled[0].first == h.limits.saveDelayMs);
        REQUIRE(h.drive([&] { return h.model->queueWrites() == 1; }));
        CHECK(h.queueFile().queue.entries.size() == 3);
        h.model->pause();
        h.model->moveToTop(102);

        h.restart();
        CHECK(h.queued() == std::vector<int>{102, 101, 103});
        CHECK(h.model->queue().paused);

        h.model->setServer("http://other:4567/");
        REQUIRE(h.drive([&] { return h.model->loaded(); }));
        CHECK(h.model->queue().entries.empty());
        h.model->setServer(kUrl);
        REQUIRE(h.drive([&] { return h.model->loaded(); }));
        CHECK(h.queued().size() == 3);
    }

    TEST_CASE("switch downloads: the start-up scan deletes leftovers and falls back to manga.json's .bak; a damaged manga is kept") {
        Harness h;
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(101, 1)});
        REQUIRE(h.downloaded(101));
        h.model->enqueue(paperGarden(), {chapter(102, 2)});
        REQUIRE(h.downloaded(102));
        // Leftovers of a stopped delete: a chapter folder and a manga folder no index lists and nothing queued.
        const std::string strayChapter = core::switchChapterDir(h.key(), kMangaId, 555);
        const std::string strayManga = core::switchMangaDir(h.key(), 99);
        REQUIRE(h.fs.makeDirs(strayChapter));
        REQUIRE(h.fs.writeAtomic(strayChapter + "/0000.jpg", jpg()));
        REQUIRE(h.fs.makeDirs(strayManga + "/c5"));
        REQUIRE(h.fs.writeAtomic(strayManga + "/c5/0000.jpg", jpg()));
        // A manga whose manga.json and .bak can't be read.
        const std::string damaged = core::switchMangaDir(h.key(), 8);
        REQUIRE(h.fs.makeDirs(damaged + "/c9"));
        REQUIRE(h.fs.writeAtomic(damaged + "/" + core::kSwitchMangaFile, "{not json"));
        // Manga 7's manga.json breaks: its .bak (chapter 101 only) is used, so chapter 102's folder is a leftover too.
        REQUIRE(h.fs.writeAtomic(h.mangaDir() + "/" + core::kSwitchMangaFile, "garbage"));

        h.restart();
        CHECK_FALSE(h.fs.exists(strayChapter));
        CHECK_FALSE(h.fs.exists(strayManga));
        CHECK(h.model->chapterState(101).state == State::OnSwitch);
        CHECK(h.model->chapterState(102).state == State::None);
        CHECK_FALSE(h.fs.exists(h.chapterDir(102)));
        CHECK(h.fs.exists(damaged));
        const auto rows = h.model->downloadedManga();
        REQUIRE(rows.size() == 2);
        CHECK(std::count_if(rows.begin(), rows.end(), [](const auto& r) { return r.damaged && r.mangaId == 8; }) == 1);
        CHECK(h.model->deleteManga(8));
        h.filesDone();
        CHECK_FALSE(h.fs.exists(damaged));
    }

    TEST_CASE("switch downloads: a damaged page; Retry fetches only the damaged pages; delete after reading empties the folder") {
        Harness h;
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(101, 1)});
        REQUIRE(h.downloaded(101));
        h.model->markDamaged(101, 1);
        CHECK(h.model->chapterState(101).state == State::Damaged);
        h.filesDone();
        REQUIRE(h.mangaFile().manga.chapters.size() == 1);
        CHECK(h.mangaFile().manga.chapters[0].damaged == std::vector<int>{1});
        CHECK(h.model->downloadedManga()[0].damagedChapters == 1);
        CHECK(h.model->retry(101));
        CHECK(h.model->chapterState(101).state != State::OnSwitch);
        REQUIRE(h.downloaded(101));
        CHECK(h.server.got(1, 0) == 1);
        CHECK(h.server.got(1, 1) == 2);
        CHECK(h.mangaFile().manga.chapters[0].damaged.empty());

        CHECK(h.model->deleteChapters({101}, SwitchDownloads::Action::DeleteAfterReading));
        CHECK(h.changes.back().action == SwitchDownloads::Action::DeleteAfterReading);
        CHECK(h.changes.back().actionChapters == 1);
        h.filesDone();
        CHECK_FALSE(h.fs.exists(h.mangaDir()));
        CHECK(h.model->downloadedManga().empty());
        CHECK(h.model->space().used == 0);
    }

    TEST_CASE("switch downloads: a cover that isn't an image is no cover; an unusable page is kept as '-'") {
        Harness h;
        h.server.cover = "not an image";
        h.server.chapters[105] = {5, {jpg(), "", gif()}};
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(105, 5)});
        REQUIRE(h.downloaded(105));
        CHECK_FALSE(h.fs.exists(h.mangaDir() + "/" + core::kSwitchCoverFile));
        CHECK(h.model->downloadedManga()[0].coverFile.empty());
        CHECK(h.mangaFile().manga.chapters[0].formats == "j-g");
        const auto files = h.model->pageFiles(105);
        REQUIRE(files.has_value());
        CHECK((*files)[1].empty());
    }

    TEST_CASE("switch downloads: while memory is low no new page request starts") {
        Harness h;
        h.memoryLow = true;
        h.model->connected();
        h.model->enqueue(paperGarden(), {chapter(102, 2)});
        REQUIRE(h.drive([&] { return h.model->wait() == core::SwitchWait::Memory; }));
        CHECK(h.server.got(2, 0) == 0);
        h.memoryLow = false;
        REQUIRE(h.downloaded(102));
    }

    TEST_CASE("switch downloads store: writes stop while not allowed; listPages reads formats and drops .tmp files") {
        TempRoot tmp;
        const platform::SafeFs fs(tmp.root);
        MainQueue main;
        DownloadStore store(fs, {}, main.deliver());
        auto bytes = std::make_shared<const std::string>(jpg());
        std::optional<bool> written;
        store.writeFile("downloads/k/m1/c2/0000.jpg", bytes, [&](bool ok) { written = ok; });
        REQUIRE(main.runUntil([&] { return written.has_value(); }));
        CHECK(*written);
        REQUIRE(fs.writeAtomic("downloads/k/m1/c2/0001.png", png()));
        REQUIRE(fs.writeAtomic("downloads/k/m1/c2/0002.jpg.tmp", "half"));
        REQUIRE(fs.writeAtomic("downloads/k/m1/c2/notes.txt", "?"));
        std::optional<std::map<int, DownloadStore::PageFile>> pages;
        store.listPages("downloads/k/m1/c2", [&](std::map<int, DownloadStore::PageFile> found) { pages = found; });
        REQUIRE(main.runUntil([&] { return pages.has_value(); }));
        REQUIRE(pages->size() == 2);
        CHECK(pages->at(0).format == core::kPageJpeg);
        CHECK(pages->at(1).format == core::kPagePng);
        CHECK(pages->at(1).bytes == png().size());
        CHECK_FALSE(fs.exists("downloads/k/m1/c2/0002.jpg.tmp"));

        store.setWritesAllowed(false);
        written.reset();
        store.writeFile("downloads/k/m1/c2/0003.jpg", bytes, [&](bool ok) { written = ok; });
        REQUIRE(main.runUntil([&] { return written.has_value(); }));
        CHECK_FALSE(*written);
        CHECK_FALSE(fs.exists("downloads/k/m1/c2/0003.jpg"));
    }
}
