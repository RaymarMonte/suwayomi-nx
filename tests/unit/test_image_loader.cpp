// image: the disk cache and the ImageLoader (WP-07), with net::FakeHttpClient serving the fixture images. The test thread plays
// the main thread: the loader's answers are queued (options.deliver) and run by MainQueue::runUntil().

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "Fixtures.hpp"
#include "core/RuntimeProfile.hpp"
#include "image/DiskCache.hpp"
#include "image/ImageLoader.hpp"
#include "net/FakeHttpClient.hpp"
#include "platform/SafeFs.hpp"

using namespace snx;
using image::ImageErrorKind;

namespace {

// A fresh folder for a SafeFs, removed at the end.
struct TempRoot {
    std::string parent;
    std::string root;
    TempRoot() {
        char pattern[] = "/tmp/snx-image-XXXXXX";
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

class MainQueue {
  public:
    std::function<void(std::function<void()>)> deliver() {
        return [this](std::function<void()> task) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tasks_.push_back(std::move(task));
            }
            wake_.notify_all();
        };
    }
    // Runs what the loader delivers until `done()`; false after 10 s without it.
    bool runUntil(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done()) {
            std::vector<std::function<void()>> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!wake_.wait_until(lock, deadline, [this] { return !tasks_.empty(); })) {
                    return done();
                }
                batch.swap(tasks_);
            }
            for (auto& task : batch) {
                task();
            }
        }
        return true;
    }

  private:
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::function<void()>> tasks_;
};

// The fake server: URL -> fixture file. Anything else is a 404.
std::map<std::string, std::string> fixtureServer() {
    return {
        {"http://s/alpha.jpg", readFixture("images/alpha-p1.jpg")},
        {"http://s/beta.png", readFixture("images/beta-p1.png")},
        {"http://s/spread.png", readFixture("images/beta-spread.png")},
        {"http://s/strip.webp", readFixture("images/gamma-strip.webp")},
        {"http://s/small.gif", readFixture("images/small.gif")},
        {"http://s/corrupt.jpg", readFixture("images/broken-corrupt.jpg")},
        {"http://s/empty.jpg", ""},
        {"http://s/tiny.jpg", readFixture("images/broken-tiny.jpg")},
        {"http://s/text.txt", "hello, not an image"},
    };
}

void serveFixtures(net::FakeHttpClient& http) {
    const auto files = fixtureServer();
    http.setHandler([files](const net::Request& request) {
        const auto found = files.find(request.url);
        return found == files.end() ? net::FakeHttpClient::answer(404) : net::FakeHttpClient::answer(200, found->second);
    });
}

image::LoaderOptions testOptions(MainQueue& main) {
    const core::RuntimeProfile profile = core::makeRuntimeProfile(core::LaunchMode::Application, 4 * core::kGiB, 100 * core::kMiB);
    image::LoaderOptions options = image::loaderOptionsFrom(profile);
    options.deliver = main.deliver();
    options.authRetryMs = 20;
    return options;
}

// Collects the "image" events (they come from loader threads).
struct Events {
    std::mutex mutex;
    std::vector<core::testevents::ImageEvent> list;
    void watch(image::ImageLoader& loader) {
        loader.setEventObserver([this](const core::testevents::ImageEvent& event) {
            std::lock_guard<std::mutex> lock(mutex);
            list.push_back(event);
        });
    }
    std::vector<core::testevents::ImageEvent> all() {
        std::lock_guard<std::mutex> lock(mutex);
        return list;
    }
};

// One load's answer.
struct Answer {
    std::optional<image::ImageResult> result;
    image::LoadCallback callback() {
        return [this](const image::ImageResult& r) { result = r; };
    }
};

image::LoadRequest page(const std::string& url, core::ImageSize box = {1280, 720},
                        net::Priority priority = net::Priority::Interactive) {
    return image::LoadRequest{url, box, priority, image::ImageKind::Page};
}

}  // namespace

TEST_SUITE("image") {
    TEST_CASE("DiskCache: written once, read back after a restart, budget, broken files, free space, stop") {
        TempRoot tmp;
        const platform::SafeFs fs(tmp.root);
        std::uint64_t free = 100 * core::kGiB;
        image::DiskCacheOptions options;
        options.budgetBytes = 3 * 1100;  // room for three of the 1000-byte entries below (each has a small header)
        options.minFreeBytes = 1 * core::kGiB;
        options.maxEntryBytes = 2000;
        options.freeBytes = [&free]() -> std::optional<std::uint64_t> { return free; };
        const std::string data(1000, 'x');

        {
            image::DiskCache cache(fs, options);
            CHECK(cache.write("http://s/1", data) == image::DiskCache::Write::Written);
            CHECK(cache.write("http://s/1", data) == image::DiskCache::Write::AlreadyThere);
            CHECK(cache.write("http://s/big", std::string(2001, 'x')) == image::DiskCache::Write::TooLarge);
            REQUIRE(cache.read("http://s/1").has_value());
            CHECK(*cache.read("http://s/1") == data);
            CHECK_FALSE(cache.read("http://s/never").has_value());
            CHECK(cache.write("http://s/2", data) == image::DiskCache::Write::Written);
        }
        // A new cache over the same folder (an app restart) finds both files.
        image::DiskCache cache(fs, options);
        CHECK(cache.count() == 2);
        CHECK(cache.read("http://s/2").value_or("") == data);
        // Budget: a fourth entry pushes out the least recently used one (1: 2 was just read).
        CHECK(cache.write("http://s/3", data) == image::DiskCache::Write::Written);
        CHECK(cache.write("http://s/4", data) == image::DiskCache::Write::Written);
        CHECK(cache.count() == 3);
        CHECK_FALSE(fs.exists(cache.pathOf(image::DiskCache::keyOf("http://s/1"))));
        CHECK(cache.read("http://s/2").has_value());
        CHECK(cache.bytes() <= options.budgetBytes);

        // A file with a flipped byte, or cut short, counts as missing and is deleted.
        const std::string path3 = cache.pathOf(image::DiskCache::keyOf("http://s/3"));
        std::string broken = fs.readFile(path3, 1 << 20).value_or("");
        REQUIRE(broken.size() > 100);
        broken[broken.size() - 10] ^= 0x55;
        REQUIRE(fs.writeAtomic(path3, broken));
        CHECK_FALSE(cache.read("http://s/3").has_value());
        CHECK_FALSE(fs.exists(path3));
        const std::string path4 = cache.pathOf(image::DiskCache::keyOf("http://s/4"));
        REQUIRE(fs.writeAtomic(path4, fs.readFile(path4, 1 << 20).value_or("").substr(0, 500)));
        CHECK_FALSE(cache.read("http://s/4").has_value());

        // Free-space guard (S5): nothing is written if the card would drop below minFreeBytes, or if nobody knows.
        free = options.minFreeBytes + 500;
        CHECK(cache.write("http://s/5", data) == image::DiskCache::Write::NoSpace);
        options.freeBytes = [] { return std::optional<std::uint64_t>(); };
        image::DiskCache unknown(fs, options);
        CHECK(unknown.write("http://s/5", data) == image::DiskCache::Write::NoSpace);
        free = 100 * core::kGiB;

        // Leftovers and strangers go at the next scan.
        REQUIRE(fs.makeDirs("cache/images/ab"));
        REQUIRE(fs.writeAtomic("cache/images/ab/0123456789abcdef.img.tmp", "half"));
        REQUIRE(fs.writeAtomic("cache/images/stray.txt", "?"));
        REQUIRE(fs.makeDirs("cache/images/notours"));
        cache.scan();
        CHECK_FALSE(fs.exists("cache/images/ab/0123456789abcdef.img.tmp"));
        CHECK_FALSE(fs.exists("cache/images/stray.txt"));
        CHECK_FALSE(fs.exists("cache/images/notours"));
        CHECK(cache.read("http://s/2").has_value());

        cache.stop();
        CHECK(cache.write("http://s/6", data) == image::DiskCache::Write::Stopped);
        CHECK(cache.read("http://s/2").has_value());  // reading still works
    }

    TEST_CASE("ImageLoader: loads on its threads, answers on the main thread, then hits the memory cache") {
        net::FakeHttpClient http;
        serveFixtures(http);
        MainQueue main;
        image::ImageLoader loader(http, testOptions(main));
        Events events;
        events.watch(loader);

        Answer first;
        std::thread::id answeredOn;
        loader.load(page("http://s/alpha.jpg"), net::CancelToken(), [&](const image::ImageResult& r) {
            first.result = r;
            answeredOn = std::this_thread::get_id();
        });
        REQUIRE(main.runUntil([&] { return first.result.has_value(); }));
        CHECK(answeredOn == std::this_thread::get_id());
        REQUIRE(first.result->ok());
        const image::ImagePtr img = first.result->value();
        CHECK(img->size() == core::ImageSize{500, 750});  // JPEG at 1/2: within the profile's 15 % slack of 480x720
        CHECK(img->sourceSize() == core::ImageSize{1000, 1500});
        CHECK(img->tiles().size() == 1);
        CHECK(http.requests().size() == 1);
        CHECK(http.requests()[0].maxBodyBytes == 32 * core::kMiB);  // S6: the download limit
        CHECK(loader.cached("http://s/alpha.jpg", {1280, 720}, image::ImageKind::Page) == img);
        CHECK(loader.cached("http://s/alpha.jpg", {640, 360}, image::ImageKind::Page) == nullptr);

        Answer second;
        loader.load(page("http://s/alpha.jpg"), net::CancelToken(), second.callback());
        REQUIRE(main.runUntil([&] { return second.result.has_value(); }));
        CHECK(second.result->value() == img);
        CHECK(http.requests().size() == 1);
        const auto seen = events.all();
        REQUIRE(seen.size() == 2);
        CHECK(seen[0].source == "net");
        CHECK(seen[0].format == "jpeg");
        CHECK(seen[0].width == 500);
        CHECK(seen[0].fileBytes == 58437);
        CHECK(seen[1].source == "memory");
        CHECK(loader.pageCacheBytes() >= img->bytes());
        CHECK(loader.coverCacheBytes() == 0);
        CHECK(loader.activeJobs() == 0);
    }

    TEST_CASE("ImageLoader: every kind of answer") {
        net::FakeHttpClient http;
        serveFixtures(http);
        MainQueue main;
        image::ImageLoader loader(http, testOptions(main));
        struct Case {
            const char* url;
            core::ImageSize box;
            bool ok;
            ImageErrorKind kind;
            core::ImageSize size;
        };
        const Case cases[] = {
            {"http://s/beta.png", {1280, 720}, true, ImageErrorKind::Corrupt, {480, 720}},
            // The box is capped at 2x the screen (1920x1440); 2000x1500 is within the 15 % slack, so it stays.
            {"http://s/spread.png", {10000, 10000}, true, ImageErrorKind::Corrupt, {2000, 1500}},
            {"http://s/spread.png", {1280, 720}, true, ImageErrorKind::Corrupt, {960, 720}},
            {"http://s/strip.webp", {1280, 0}, true, ImageErrorKind::Corrupt, {800, 12000}},
            {"http://s/small.gif", {0, 0}, true, ImageErrorKind::Corrupt, {100, 150}},
            {"http://s/tiny.jpg", {1280, 720}, true, ImageErrorKind::Corrupt, {1, 1}},
            {"http://s/corrupt.jpg", {1280, 720}, false, ImageErrorKind::Corrupt, {}},
            {"http://s/empty.jpg", {1280, 720}, false, ImageErrorKind::Corrupt, {}},
            {"http://s/text.txt", {1280, 720}, false, ImageErrorKind::Unsupported, {}},
            {"http://s/missing.jpg", {1280, 720}, false, ImageErrorKind::Http, {}},
        };
        for (const Case& c : cases) {
            CAPTURE(c.url);
            Answer answer;
            loader.load(page(c.url, c.box), net::CancelToken(), answer.callback());
            REQUIRE(main.runUntil([&] { return answer.result.has_value(); }));
            REQUIRE(answer.result->ok() == c.ok);
            if (c.ok) {
                CHECK(answer.result->value()->size() == c.size);
            } else {
                CHECK(answer.result->error().kind == c.kind);
            }
        }
        // The strip is cut into GPU-sized tiles.
        CHECK(loader.cached("http://s/strip.webp", {1280, 0}, image::ImageKind::Page)->tiles().size() == 6);
        // No answer at all is a network error.
        http.setHandler([](const net::Request&) { return net::HttpResult::fail(core::ErrorKind::Network, "refused"); });
        Answer offline;
        loader.load(page("http://s/other.jpg"), net::CancelToken(), offline.callback());
        REQUIRE(main.runUntil([&] { return offline.result.has_value(); }));
        CHECK(offline.result->error().kind == ImageErrorKind::Network);
    }

    TEST_CASE("ImageLoader: loads of one URL share one download, more urgent ones go first") {
        net::FakeHttpClient http;
        serveFixtures(http);
        http.hold(true);
        MainQueue main;
        image::ImageLoader loader(http, testOptions(main));
        Answer a, b, c;
        loader.load(page("http://s/alpha.jpg", {1280, 720}, net::Priority::Prefetch), net::CancelToken(), a.callback());
        loader.load(page("http://s/alpha.jpg"), net::CancelToken(), b.callback());
        loader.load(page("http://s/alpha.jpg", {300, 300}), net::CancelToken(), c.callback());  // another box, same file
        CHECK(http.waitingCount() == 1);
        CHECK(loader.activeJobs() == 1);
        http.releaseAll();
        REQUIRE(main.runUntil([&] { return a.result && b.result && c.result; }));
        CHECK(http.requests().size() == 1);
        REQUIRE(a.result->ok());
        CHECK(a.result->value() == b.result->value());  // the very same decoded image
        REQUIRE(c.result->ok());
        CHECK(c.result->value()->size() == core::ImageSize{200, 300});
    }

    TEST_CASE("ImageLoader: a cancelled load answers Cancelled, stops its download, and never a late image") {
        net::FakeHttpClient http;
        serveFixtures(http);
        http.hold(true);
        MainQueue main;
        image::ImageLoader loader(http, testOptions(main));
        net::CancelToken screen;
        Answer waiting;
        loader.load(page("http://s/alpha.jpg"), screen, waiting.callback());
        screen.cancel();
        // The loader notices within ~50 ms that nobody wants it any more and cancels the download.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        http.releaseAll();
        REQUIRE(main.runUntil([&] { return waiting.result.has_value(); }));
        REQUIRE_FALSE(waiting.result->ok());
        CHECK(waiting.result->error().kind == ImageErrorKind::Cancelled);
        CHECK(loader.cached("http://s/alpha.jpg", {1280, 720}, image::ImageKind::Page) == nullptr);

        // Cancelled while it decodes (the download is done at once): still Cancelled, never the image.
        http.hold(false);
        net::CancelToken mid;
        Answer decoding;
        loader.load(page("http://s/spread.png"), mid, decoding.callback());
        mid.cancel();
        REQUIRE(main.runUntil([&] { return decoding.result.has_value(); }));
        CHECK(decoding.result->error().kind == ImageErrorKind::Cancelled);
        // A cancelled caller doesn't spoil the load for another one of the same URL.
        net::CancelToken gone;
        Answer kept, dropped;
        http.hold(true);
        loader.load(page("http://s/beta.png"), gone, dropped.callback());
        loader.load(page("http://s/beta.png"), net::CancelToken(), kept.callback());
        gone.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        http.releaseAll();
        REQUIRE(main.runUntil([&] { return kept.result && dropped.result; }));
        CHECK(kept.result->ok());
        CHECK(dropped.result->error().kind == ImageErrorKind::Cancelled);
        // Already cancelled: answered at once, nothing requested.
        const std::size_t before = http.requests().size();
        Answer late;
        loader.load(page("http://s/small.gif"), gone, late.callback());
        REQUIRE(main.runUntil([&] { return late.result.has_value(); }));
        CHECK(late.result->error().kind == ImageErrorKind::Cancelled);
        CHECK(http.requests().size() == before);
    }

    TEST_CASE("ImageLoader: an HTTP 401 is tried once more (ui_login renewal), a second 401 is an error") {
        net::FakeHttpClient http;
        const std::string jpeg = readFixture("images/alpha-p1.jpg");
        std::atomic<int> calls{0};
        std::atomic<int> refusals{1};
        http.setHandler([&](const net::Request&) {
            ++calls;
            if (refusals.load() > 0) {
                --refusals;
                return net::FakeHttpClient::answer(401);
            }
            return net::FakeHttpClient::answer(200, jpeg);
        });
        MainQueue main;
        image::ImageLoader loader(http, testOptions(main));
        Answer renewed;
        loader.load(page("http://s/a.jpg"), net::CancelToken(), renewed.callback());
        REQUIRE(main.runUntil([&] { return renewed.result.has_value(); }));
        CHECK(renewed.result->ok());
        CHECK(calls.load() == 2);
        refusals = 5;
        Answer refused;
        loader.load(page("http://s/b.jpg"), net::CancelToken(), refused.callback());
        REQUIRE(main.runUntil([&] { return refused.result.has_value(); }));
        REQUIRE_FALSE(refused.result->ok());
        CHECK(refused.result->error().kind == ImageErrorKind::Http);
        CHECK(refused.result->error().httpStatus == 401);
        CHECK(calls.load() == 4);
    }

    TEST_CASE("ImageLoader: a full queue answers Busy; shutdown answers Cancelled") {
        net::FakeHttpClient http;
        serveFixtures(http);
        http.hold(true);
        MainQueue main;
        image::LoaderOptions options = testOptions(main);
        options.queueLimit = 2;
        image::ImageLoader loader(http, options);
        Answer a, b, c;
        loader.load(page("http://s/alpha.jpg"), net::CancelToken(), a.callback());
        loader.load(page("http://s/beta.png"), net::CancelToken(), b.callback());
        loader.load(page("http://s/small.gif"), net::CancelToken(), c.callback());
        REQUIRE(main.runUntil([&] { return c.result.has_value(); }));
        CHECK(c.result->error().kind == ImageErrorKind::Busy);
        loader.shutdown();
        REQUIRE(main.runUntil([&] { return a.result && b.result; }));
        CHECK(a.result->error().kind == ImageErrorKind::Cancelled);
        CHECK(b.result->error().kind == ImageErrorKind::Cancelled);
        Answer after;
        loader.load(page("http://s/alpha.jpg"), net::CancelToken(), after.callback());
        REQUIRE(main.runUntil([&] { return after.result.has_value(); }));
        CHECK(after.result->error().kind == ImageErrorKind::Cancelled);
        http.releaseAll();  // the held requests end (Cancelled); their callbacks find no loader and do nothing
    }

    TEST_CASE("ImageLoader: memory pressure shrinks the caches, pauses prefetch, and recovers") {
        net::FakeHttpClient http;
        serveFixtures(http);
        MainQueue main;
        image::LoaderOptions options = testOptions(main);
        options.pageCacheBytes = 4 * core::kMiB;  // three 480x720 pages fit (1.3 MiB each)
        std::atomic<std::uint64_t> used{100 * core::kMiB};
        options.memory = [&used]() -> std::optional<core::MemorySample> {
            return core::MemorySample{used.load(), 1000 * core::kMiB};
        };
        options.pressureCheckMs = 3600 * 1000;  // the test calls checkMemoryNow() itself
        image::ImageLoader loader(http, options);
        std::vector<std::pair<int, std::string>> changes;
        loader.setPressureObserver([&](int percent, const char* action) { changes.emplace_back(percent, action); });
        for (const char* url : {"http://s/alpha.jpg", "http://s/beta.png", "http://s/tiny.jpg"}) {
            Answer answer;
            loader.load(page(url), net::CancelToken(), answer.callback());
            REQUIRE(main.runUntil([&] { return answer.result.has_value(); }));
            REQUIRE(answer.result->ok());
        }
        const std::uint64_t full = loader.pageCacheBytes();
        CHECK(full > 2 * core::kMiB);

        used = 900 * core::kMiB;  // 90 %: over the 85 % line
        loader.checkMemoryNow();
        CHECK(loader.prefetchPaused());
        CHECK(loader.pageCacheBytes() <= 2 * core::kMiB);
        REQUIRE(changes.size() == 1);
        CHECK(changes[0] == std::make_pair(90, std::string("shrink_caches")));
        Answer prefetch;
        loader.load(page("http://s/small.gif", {0, 0}, net::Priority::Prefetch), net::CancelToken(), prefetch.callback());
        REQUIRE(main.runUntil([&] { return prefetch.result.has_value(); }));
        CHECK(prefetch.result->error().kind == ImageErrorKind::Paused);
        Answer needed;  // what the user is looking at still loads
        loader.load(page("http://s/small.gif", {0, 0}), net::CancelToken(), needed.callback());
        REQUIRE(main.runUntil([&] { return needed.result.has_value(); }));
        CHECK(needed.result->ok());

        used = 500 * core::kMiB;
        loader.checkMemoryNow();
        CHECK_FALSE(loader.prefetchPaused());
        REQUIRE(changes.size() == 2);
        CHECK(changes[1].second == "recovered");
    }

    TEST_CASE("ImageLoader: pages and covers have their own caches; the disk cache survives a restart") {
        TempRoot tmp;
        const platform::SafeFs fs(tmp.root);
        net::FakeHttpClient http;
        serveFixtures(http);
        MainQueue main;
        image::LoaderOptions options = testOptions(main);
        options.fs = &fs;
        options.disk.freeBytes = [] { return std::optional<std::uint64_t>(100 * core::kGiB); };
        {
            image::ImageLoader loader(http, options);
            Answer cover;
            loader.load(image::LoadRequest{"http://s/alpha.jpg", {300, 450}, net::Priority::Interactive, image::ImageKind::Cover},
                        net::CancelToken(), cover.callback());
            REQUIRE(main.runUntil([&] { return cover.result.has_value(); }));
            REQUIRE(cover.result->ok());
            CHECK(loader.coverCacheBytes() > 0);
            CHECK(loader.pageCacheBytes() == 0);
            // The file goes to the disk cache in the background, after the answer.
            for (int i = 0; i < 200 && loader.diskCache()->count() == 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            CHECK(loader.diskCache()->count() == 1);
        }
        CHECK(http.requests().size() == 1);
        image::ImageLoader restarted(http, options);
        Events events;
        events.watch(restarted);
        Answer again;
        restarted.load(page("http://s/alpha.jpg"), net::CancelToken(), again.callback());
        REQUIRE(main.runUntil([&] { return again.result.has_value(); }));
        REQUIRE(again.result->ok());
        CHECK(http.requests().size() == 1);  // no download: it came from the disk
        REQUIRE(events.all().size() == 1);
        CHECK(events.all()[0].source == "disk");
        // A broken download is never cached on disk.
        Answer broken;
        restarted.load(page("http://s/corrupt.jpg"), net::CancelToken(), broken.callback());
        REQUIRE(main.runUntil([&] { return broken.result.has_value(); }));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(restarted.diskCache()->count() == 1);
    }
}
