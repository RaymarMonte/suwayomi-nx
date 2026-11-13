// WP-15b: the waiting-save queue in the app (src/ui/reader/WaitingSaves) on a FakeHttpClient: a save that fails as the reader
// closes goes in (a later one for the same chapter replaces it), a round after a nudge sends it (a spot goes as
// UpdateStripProgress), "already" and "server moved" send nothing, a server that is away keeps it and backs off, three other
// errors drop it, Auth waits for connected(), the readers take an entry over when they open its chapter, and a new queue on
// the same folder (a restart) still has it.
// The client answers inside the call (no deliver); the loader's answers come through MainQueue (the test plays the main thread).
#include <sys/stat.h>
#include <unistd.h>

#include <doctest/doctest.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "ApiTestKit.hpp"
#include "Fixtures.hpp"
#include "MainQueue.hpp"
#include "api/SuwayomiClient.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/StripMath.hpp"
#include "image/ImageLoader.hpp"
#include "net/FakeHttpClient.hpp"
#include "platform/SafeFs.hpp"
#include "ui/reader/ReaderModel.hpp"
#include "ui/reader/StripModel.hpp"
#include "ui/reader/WaitingSaves.hpp"

using namespace snx;
using net::FakeHttpClient;
using net::Request;
using ui::WaitingSaves;

namespace {

constexpr int kMangaId = 1;

struct FakeChapter {
    int id = 0;
    int sourceOrder = 0;
    int pages = 10;
    bool isRead = false;
    int lastPageRead = 0;
    std::int64_t lastReadAt = 0;
    std::string note;  // snx_strip_spot
};

// Three chapters (101..103, 10 pages each). Every progress save stamps lastReadAt with the next tick, like the server.
struct FakeServer {
    std::vector<FakeChapter> chapters{{101, 1, 10, false, 0, 0, {}}, {102, 2, 10, false, 0, 0, {}}, {103, 3, 10, false, 0, 0, {}}};
    bool offline = false;      // no answer at all
    int stateStatus = 200;     // ChapterState's HTTP status (500: a server error, 401: Auth)
    bool failSaves = false;    // progress saves answer 500
    std::int64_t tick = 1000;  // the server's clock for lastReadAt
    std::vector<nlohmann::json> progress;  // every progress save: the patch with "id" (and "spot")
    std::string image = readFixture("images/alpha-p1.jpg");

    FakeChapter* find(int id) {
        for (FakeChapter& c : chapters) {
            if (c.id == id) {
                return &c;
            }
        }
        return nullptr;
    }

    nlohmann::json chapterJson(const FakeChapter& c) const {
        nlohmann::json meta = nlohmann::json::array();
        if (!c.note.empty()) {
            meta.push_back({{"key", "snx_strip_spot"}, {"value", c.note}});
        }
        return {{"id", c.id}, {"mangaId", kMangaId}, {"sourceOrder", c.sourceOrder}, {"isRead", c.isRead},
                {"lastPageRead", c.lastPageRead}, {"pageCount", c.pages}, {"lastReadAt", std::to_string(c.lastReadAt)},
                {"meta", meta}};
    }

    net::HttpResult answer(const Request& request) {
        if (offline) {
            return net::HttpResult::fail(core::ErrorKind::Network, "Connection refused");
        }
        if (request.url.rfind("http://s/api/v1/manga/1/chapter/", 0) == 0) {
            return FakeHttpClient::answer(200, image);
        }
        const std::string operation = operationOf(request);
        const nlohmann::json vars = variablesOf(request);
        if (operation == "FetchChapterPages") {
            FakeChapter* c = find(vars["chapterId"].get<int>());
            REQUIRE(c != nullptr);
            nlohmann::json pages = nlohmann::json::array();
            for (int i = 0; i < c->pages; ++i) {
                pages.push_back("/api/v1/manga/1/chapter/" + std::to_string(c->sourceOrder) + "/page/" + std::to_string(i));
            }
            return FakeHttpClient::answer(200, nlohmann::json{{"data", {{"fetchChapterPages", {
                {"pages", pages}, {"chapter", chapterJson(*c)}, {"syncConflict", nullptr}}}}}}.dump());
        }
        if (operation == "ChapterState") {
            if (stateStatus != 200) {
                return FakeHttpClient::answer(stateStatus, "no");
            }
            FakeChapter* c = find(vars["id"].get<int>());
            if (c == nullptr) {
                return FakeHttpClient::answer(200, R"({"errors":[{"message":"null","path":["chapter"]}]})");
            }
            return FakeHttpClient::answer(200, nlohmann::json{{"data", {{"chapter", chapterJson(*c)}}}}.dump());
        }
        if (operation == "UpdateChapterProgress" || operation == "UpdateStripProgress") {
            if (failSaves) {
                return FakeHttpClient::answer(500, "oops");
            }
            FakeChapter* c = find(vars["id"].get<int>());
            REQUIRE(c != nullptr);
            nlohmann::json patch = vars["patch"];
            patch["id"] = c->id;
            c->lastPageRead = patch["lastPageRead"].get<int>();
            c->lastReadAt = ++tick;
            if (patch.contains("isRead") && patch["isRead"].get<bool>()) {
                c->isRead = true;
            }
            nlohmann::json data = {{"updateChapter", {{"chapter", {{"id", c->id}, {"lastPageRead", c->lastPageRead},
                {"isRead", c->isRead}, {"pageCount", c->pages}, {"lastReadAt", std::to_string(c->lastReadAt)}}}}}};
            if (operation == "UpdateStripProgress") {
                c->note = vars["spot"].get<std::string>();
                patch["spot"] = c->note;
                data["setChapterMeta"] = {{"meta", {{"key", "snx_strip_spot"}, {"value", c->note}}}};
            }
            progress.push_back(patch);
            return FakeHttpClient::answer(200, nlohmann::json{{"data", data}}.dump());
        }
        return FakeHttpClient::answer(404);
    }
};

// A temp folder for the queue's file, removed again at the end.
struct TempRoot {
    std::string parent;
    std::string root;
    TempRoot() {
        char pattern[] = "/tmp/snx-queue-XXXXXX";
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

struct Report {
    std::string what;
    int chapterId;
    int page;
    std::string reason;
    std::size_t waiting;
};

struct Harness {
    FakeServer server;
    FakeHttpClient http;
    MainQueue main;
    std::unique_ptr<api::SuwayomiClient> client;
    std::unique_ptr<image::ImageLoader> images;
    std::int64_t now = 1000;
    std::vector<std::pair<int, std::function<void()>>> timers;  // later(): the test runs them
    std::vector<Report> reports;
    std::vector<int> heard;  // listeners: the chapters gone (sent, already, dropped in a round)
    std::vector<int> added;  // listeners: the chapters added
    std::unique_ptr<WaitingSaves> queue;

    explicit Harness(const platform::SafeFs* fs = nullptr) {
        http.setHandler([this](const Request& request) { return server.answer(request); });
        core::ServerSettings settings;
        settings.url = "http://s";
        client = std::make_unique<api::SuwayomiClient>(http, settings, api::ClientOptions{});
        const core::RuntimeProfile profile =
            core::makeRuntimeProfile(core::LaunchMode::Application, 4 * core::kGiB, 100 * core::kMiB);
        image::LoaderOptions options = image::loaderOptionsFrom(profile);
        options.deliver = main.deliver();
        images = std::make_unique<image::ImageLoader>(http, options);
        makeQueue(fs);
        // As in main(): any HTTP answer nudges the queue.
        http.addResponseObserver([this](const Request&, const net::HttpResult& result) {
            if (result.ok() && queue) {
                queue->serverAnswered();
            }
        });
    }

    ~Harness() {
        images.reset();  // its threads end first: no answer reaches the observer below while the queue goes
        queue.reset();
    }

    // Everything still under way ends, and its answers run.
    void drain() { main.settle(50); }

    // Runs the loader's answers until no page in the strip is loading.
    bool settle(const ui::StripModel& model) {
        const bool done = main.runUntil([&] {
            for (int item = 0; item < model.layout().size(); ++item) {
                if (model.pageAt(item).state == ui::ReaderPage::State::Loading) {
                    return false;
                }
            }
            return true;
        });
        main.settle(50);
        return done;
    }

    void makeQueue(const platform::SafeFs* fs) {
        WaitingSaves::Hooks hooks;
        hooks.clockMs = [this] { return now; };
        hooks.nowSeconds = [] { return std::int64_t{1794534794}; };
        hooks.later = [this](int ms, std::function<void()> fn) { timers.emplace_back(ms, std::move(fn)); };
        hooks.post = main.deliver();
        hooks.report = [this](const char* what, const core::WaitingSave& e, const std::string& reason, std::size_t waiting) {
            reports.push_back({what, e.chapterId, e.page, reason, waiting});
        };
        queue = std::make_unique<WaitingSaves>(*client, fs, WaitingSaves::Options{}, hooks);
        queue->addListener([this](WaitingSaves::Change change, const core::WaitingSave& e) {
            (change == WaitingSaves::Change::Gone ? heard : added).push_back(e.chapterId);
        });
    }

    // The last timer set (the back-off), run now.
    int runTimer() {
        REQUIRE_FALSE(timers.empty());
        auto [ms, fn] = std::move(timers.back());
        timers.clear();
        fn();
        return ms;
    }

    std::vector<core::Chapter> chapters() const {
        std::vector<core::Chapter> list;
        for (const FakeChapter& f : server.chapters) {
            core::Chapter c;
            c.id = f.id;
            c.mangaId = kMangaId;
            c.name = "Chapter " + std::to_string(f.sourceOrder);
            c.sourceOrder = f.sourceOrder;
            c.isRead = f.isRead;
            c.lastPageRead = f.lastPageRead;
            c.pageCount = f.pages;
            list.push_back(c);
        }
        return list;
    }

    core::Manga manga() const {
        core::Manga m;
        m.id = kMangaId;
        m.title = "Alpha";
        return m;
    }

    std::unique_ptr<ui::ReaderModel> reader() {
        ui::ReaderOptions options;
        options.clock = [this] { return now; };
        options.queue = queue.get();
        core::MangaReaderSettings settings;
        settings.direction = core::ReadingDirection::LeftToRight;
        return std::make_unique<ui::ReaderModel>(*client, *images, manga(), chapters(), settings, options);
    }

    std::unique_ptr<ui::StripModel> strip() {
        ui::StripOptions options;
        options.clock = [this] { return now; };
        options.queue = queue.get();
        return std::make_unique<ui::StripModel>(*client, *images, manga(), chapters(), options);
    }

    int asked(const std::string& operation) const {
        int n = 0;
        for (const Request& r : http.requests()) {
            n += operationOf(r) == operation ? 1 : 0;
        }
        return n;
    }

    bool reported(const std::string& what) const {
        for (const Report& r : reports) {
            if (r.what == what) {
                return true;
            }
        }
        return false;
    }

    // A waiting save made by hand, for the configured server, at the server's state now.
    core::WaitingSave entry(int chapterId, int page) {
        core::WaitingSave e;
        e.chapterId = chapterId;
        e.mangaId = kMangaId;
        e.page = page;
        const FakeChapter* c = server.find(chapterId);
        e.base = core::ServerProgress{c->lastPageRead, c->isRead, c->lastReadAt, {}};
        e.mangaTitle = "Alpha";
        e.chapterName = "Chapter " + std::to_string(c->sourceOrder);
        return e;
    }
};

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("queue: a close-save that fails goes in with its base; a later one for the same chapter replaces it") {
    Harness h;
    h.server.chapters[0].lastPageRead = 1;
    h.server.chapters[0].lastReadAt = 500;
    auto first = h.reader();
    auto second = h.reader();
    first->open(101, 1);
    second->open(101, 1);
    REQUIRE(first->state() == ui::ReaderModel::State::Ready);
    for (int i = 0; i < 3; ++i) {
        first->step(1);  // page 4
    }
    second->step(1);  // page 2
    h.server.offline = true;
    first->close(nullptr);
    REQUIRE(h.queue->size() == 1);
    const core::WaitingSave* waiting = h.queue->waitingFor(101);
    REQUIRE(waiting != nullptr);
    CHECK(waiting->page == 4);
    CHECK_FALSE(waiting->read);
    CHECK_FALSE(waiting->spot);
    CHECK(waiting->base.page == 1);
    CHECK(waiting->base.lastReadAt == 500);  // from fetchChapterPages' answer
    CHECK(waiting->serverUrl == "http://s");
    CHECK(waiting->mangaTitle == "Alpha");
    CHECK(waiting->chapterName == "Chapter 1");
    CHECK(waiting->madeAt == 1794534794);
    CHECK(h.reported("added"));
    CHECK(h.added == std::vector<int>{101});  // History shows its note
    CHECK(h.queue->waitingForManga(kMangaId) == waiting);
    CHECK(h.queue->waitingForManga(kMangaId + 1) == nullptr);
    second->close(nullptr);
    REQUIRE(h.queue->size() == 1);
    CHECK(h.queue->waitingFor(101)->page == 2);  // the newer one
    CHECK(h.queue->waitingFor(101)->base.lastReadAt == 500);
    // Nothing fails while the reader is open: only closing hands saves over.
    h.server.offline = false;
    auto third = h.reader();
    third->open(102, 0);
    third->step(1);
    h.server.offline = true;
    h.now += 6000;
    third->onFrame();  // the save fails: the reader tries again itself
    CHECK(h.queue->size() == 1);
    h.drain();
}

TEST_CASE("queue: a round after a nudge sends it; the listeners hear it; a spot goes as UpdateStripProgress") {
    Harness h;
    h.server.chapters[0].lastReadAt = 500;
    h.queue->add(h.entry(101, 4));
    core::WaitingSave strip = h.entry(102, 3);
    strip.spot = 0.25;
    strip.read = true;
    h.queue->add(strip);
    REQUIRE(h.queue->size() == 2);
    h.queue->connected();
    CHECK(h.queue->size() == 0);
    CHECK(h.asked("ChapterState") == 2);
    REQUIRE(h.server.progress.size() == 2);
    CHECK(h.server.progress[0]["id"] == 101);  // oldest first
    CHECK(h.server.progress[0]["lastPageRead"] == 4);
    CHECK(h.asked("UpdateStripProgress") == 1);
    CHECK(h.server.progress[1]["spot"] == core::stripSpotValue(3, 0.25));
    CHECK(h.server.progress[1]["isRead"] == true);
    CHECK(h.server.chapters[1].isRead);
    CHECK(h.heard == std::vector<int>{101, 102});
    CHECK(h.reports.back().what == "sent");
    CHECK(h.reports.back().waiting == 0);
}

TEST_CASE("queue: any HTTP answer nudges a round, at most one per interval") {
    Harness h;
    h.queue->add(h.entry(101, 4));
    // An answer to anything (here a page list) posts one nudge to the main thread.
    CHECK(h.client->pages(103, api::PageFormat::AsStored, net::CancelToken()).get().ok());
    CHECK(h.asked("ChapterState") == 0);  // not on the network thread
    h.main.settle(50);
    CHECK(h.asked("ChapterState") == 1);
    CHECK(h.queue->size() == 0);
    // Another entry: an answer right after the round doesn't start one (its own requests answer too) ...
    h.queue->add(h.entry(102, 2));
    CHECK(h.client->pages(103, api::PageFormat::AsStored, net::CancelToken()).get().ok());
    h.main.settle(50);
    CHECK(h.queue->size() == 1);
    // ... one after the interval does.
    h.now += 5000;
    CHECK(h.client->pages(103, api::PageFormat::AsStored, net::CancelToken()).get().ok());
    h.main.settle(50);
    CHECK(h.queue->size() == 0);
}

TEST_CASE("queue: the server has it already, or moved on: nothing is sent") {
    Harness h;
    h.queue->add(h.entry(101, 4));
    h.queue->add(h.entry(102, 4));
    h.server.chapters[0].lastPageRead = 4;  // the save got through after all
    h.server.chapters[0].lastReadAt = 600;
    h.server.chapters[1].lastPageRead = 7;  // read on elsewhere
    h.server.chapters[1].lastReadAt = 700;
    h.queue->connected();
    CHECK(h.queue->size() == 0);
    CHECK(h.server.progress.empty());
    CHECK(h.server.chapters[1].lastPageRead == 7);  // the newer server change wins
    REQUIRE(h.reports.size() == 4);
    CHECK(h.reports[2].what == "already");
    CHECK(h.reports[3].what == "dropped");
    CHECK(h.reports[3].reason == "server moved");
    CHECK(h.heard == std::vector<int>{101, 102});  // the screens reload either way (History's note goes)
}

TEST_CASE("queue: a server that is away keeps it and backs off, doubling up to the cap") {
    Harness h;
    h.queue->add(h.entry(101, 4));
    h.server.offline = true;
    h.queue->connected();
    CHECK(h.queue->size() == 1);
    CHECK(h.reports.back().what == "kept");
    CHECK(h.reports.back().reason == "offline");
    CHECK_FALSE(h.queue->roundRunning());
    CHECK(h.runTimer() == 5000);
    CHECK(h.runTimer() == 10000);
    CHECK(h.runTimer() == 20000);
    CHECK(h.runTimer() == 40000);
    CHECK(h.runTimer() == 60000);
    CHECK(h.runTimer() == 60000);
    h.server.offline = false;
    h.runTimer();
    CHECK(h.queue->size() == 0);
    CHECK(h.timers.empty());  // done: no more timers
    REQUIRE(h.server.progress.size() == 1);
    // A save that times out after going out counts the same; the next round finds it saved ("already").
}

TEST_CASE("queue: three other errors in a row drop it; a chapter that is gone too") {
    Harness h;
    h.queue->add(h.entry(101, 4));
    h.server.stateStatus = 500;
    h.queue->connected();
    CHECK(h.queue->size() == 1);
    CHECK(h.queue->waitingFor(101)->errors == 1);
    CHECK(h.timers.empty());  // the server answered: no back-off timer
    h.queue->connected();
    CHECK(h.queue->waitingFor(101)->errors == 2);
    h.queue->connected();
    CHECK(h.queue->size() == 0);
    CHECK(h.reports.back().what == "dropped");
    CHECK(h.reports.back().reason == "errors");
    CHECK(h.server.progress.empty());
    // A chapter the server no longer has: an error answer with no data, three times.
    h.server.stateStatus = 200;
    core::WaitingSave gone = h.entry(101, 4);
    gone.chapterId = 999;
    h.queue->add(gone);
    for (int i = 0; i < 3; ++i) {
        h.queue->connected();
    }
    CHECK(h.queue->size() == 0);
    // A save that fails with a server error counts too.
    h.queue->add(h.entry(102, 4));
    h.server.failSaves = true;
    h.queue->connected();
    CHECK(h.queue->waitingFor(102)->errors == 1);
}

TEST_CASE("queue: Auth ends the round until connected(); other servers' entries wait") {
    Harness h;
    h.queue->add(h.entry(101, 4));
    h.server.stateStatus = 401;
    h.queue->connected();
    CHECK(h.queue->size() == 1);
    CHECK(h.reports.back().reason == "auth");
    h.server.stateStatus = 200;
    h.now += 60000;
    CHECK(h.client->pages(103, api::PageFormat::AsStored, net::CancelToken()).get().ok());
    h.main.settle(50);
    CHECK(h.queue->size() == 1);  // answers don't start rounds while the login is refused
    h.queue->connected();          // a login worked
    CHECK(h.queue->size() == 0);

    // An entry of another server stays until that server is configured again.
    h.queue->add(h.entry(101, 6));
    core::ServerSettings other;
    other.url = "http://other";
    h.client->configure(other);
    h.queue->connected();
    CHECK(h.queue->size() == 1);
    CHECK(h.queue->waitingFor(101) == nullptr);  // not for this server
    CHECK(h.asked("ChapterState") == 2);
}

TEST_CASE("queue: the page reader takes an entry over (opens at its page, saves it); a moved server drops it") {
    SUBCASE("still at the base") {
        Harness h;
        h.server.chapters[0].lastPageRead = 1;
        h.server.chapters[0].lastReadAt = 500;
        h.queue->add(h.entry(101, 5));
        auto model = h.reader();
        model->open(101, 1);  // the caller's page: the server's
        REQUIRE(model->state() == ui::ReaderModel::State::Ready);
        CHECK(model->position() == 5);
        CHECK(h.queue->size() == 0);
        CHECK(h.reports.back().what == "taken");
        CHECK(h.server.progress.empty());  // its own progress now: saved within the interval
        h.now += 5000;
        model->onFrame();
        REQUIRE(h.server.progress.size() == 1);
        CHECK(h.server.progress[0]["lastPageRead"] == 5);
        h.drain();
    }
    SUBCASE("the server moved on") {
        Harness h;
        core::WaitingSave old = h.entry(101, 5);
        h.queue->add(old);
        h.server.chapters[0].lastPageRead = 2;  // read elsewhere since
        h.server.chapters[0].lastReadAt = 900;
        auto model = h.reader();
        model->open(101, 2);
        CHECK(model->position() == 2);
        CHECK(h.queue->size() == 0);
        CHECK(h.reports.back().what == "dropped");
        h.now += 10000;
        model->onFrame();
        CHECK(h.server.progress.empty());
        h.drain();
    }
}

TEST_CASE("queue: the strip takes an entry over at its page and spot; a strip close-save that fails keeps its spot") {
    Harness h;
    core::WaitingSave entry = h.entry(101, 3);
    entry.spot = 0.5;
    h.queue->add(entry);
    auto model = h.strip();
    model->open(101, 0);
    REQUIRE(model->state() == ui::StripModel::State::Ready);
    REQUIRE(h.settle(*model));
    const core::StripPlace middle = model->layout().middle();
    CHECK(middle.item == model->layout().find(101, 3));
    CHECK(middle.share == doctest::Approx(0.5));
    CHECK(h.queue->size() == 0);
    h.now += 5000;
    model->onFrame();
    REQUIRE(h.server.progress.size() == 1);
    CHECK(h.server.progress[0]["lastPageRead"] == 3);
    CHECK(h.server.progress[0]["spot"] == core::stripSpotValue(3, 0.5));

    // Opened at a page's top elsewhere, then closed while the server is away: the entry has the strip's spot.
    auto other = h.strip();
    other->open(102, 2);
    REQUIRE(h.settle(*other));
    h.server.offline = true;
    other->close(nullptr);
    const core::WaitingSave* waiting = h.queue->waitingFor(102);
    REQUIRE(waiting != nullptr);
    CHECK(waiting->page == 2);
    REQUIRE(waiting->spot);
    CHECK(*waiting->spot == doctest::Approx(360.0 / 960).epsilon(0.001));
    h.drain();
}

TEST_CASE("queue: a restart (a new queue on the same folder) still has it; a broken file falls back to the .bak") {
    TempRoot temp;
    const platform::SafeFs fs(temp.root);
    {
        Harness h(&fs);
        h.queue->load();
        h.queue->add(h.entry(101, 4));
        CHECK(h.queue->flush());
        CHECK(fs.exists(WaitingSaves::kFile));
        core::WaitingSave strip = h.entry(102, 6);
        strip.spot = 0.75;
        h.queue->add(strip);
        h.now += 1000;
        h.queue->onFrame(true);  // the reader moves: no write yet
        h.now += 10000;
        h.queue->onFrame(true);
        CHECK(fs.readFile(WaitingSaves::kFile, 65536)->find("\"chapterId\":102") == std::string::npos);
        h.queue->onFrame(false);  // a still frame: written now (the old file becomes the .bak)
        CHECK(fs.readFile(WaitingSaves::kFile, 65536)->find("\"chapterId\":102") != std::string::npos);
        CHECK(fs.exists(WaitingSaves::kBackupFile));
    }
    {
        Harness h(&fs);
        h.queue->load();
        REQUIRE(h.queue->size() == 2);
        const core::WaitingSave* strip = h.queue->waitingFor(102);
        REQUIRE(strip != nullptr);
        CHECK(strip->page == 6);
        CHECK(strip->spot == 0.75);
        CHECK(strip->mangaTitle == "Alpha");
    }
    // A file cut short (a power cut): the .bak (the version before the last write) is used and written back.
    REQUIRE(fs.writeAtomic(WaitingSaves::kFile, R"({"version": 1, "entr)"));
    {
        Harness h(&fs);
        h.queue->load();
        REQUIRE(h.queue->size() == 1);
        CHECK(h.queue->waitingFor(101) != nullptr);
        CHECK(h.queue->flush());
        CHECK(fs.readFile(WaitingSaves::kFile, 65536)->find("\"chapterId\":101") != std::string::npos);
    }
    // Both broken: empty.
    REQUIRE(fs.writeAtomic(WaitingSaves::kFile, "garbage"));
    REQUIRE(fs.writeAtomic(WaitingSaves::kBackupFile, "garbage"));
    {
        Harness h(&fs);
        h.queue->load();
        CHECK(h.queue->size() == 0);
    }
}

}  // TEST_SUITE
