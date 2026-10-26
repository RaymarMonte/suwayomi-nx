// core's image math and policies (WP-07): fit/plan/tiles/tile window, the byte-budget LRU, the memory-pressure decision,
// and the test config/event parts the image module uses.

#include <cstdint>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/ByteLru.hpp"
#include "core/ImageGeometry.hpp"
#include "core/MemoryPressure.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/TestConfig.hpp"
#include "core/TestEvents.hpp"

using namespace snx::core;

TEST_SUITE("core") {
    TEST_CASE("ImageGeometry: fitInside keeps the shape, never grows, 0 = no limit") {
        CHECK(fitInside({1000, 1500}, {1280, 720}) == ImageSize{480, 720});
        CHECK(fitInside({1000, 1500}, {2560, 1440}) == ImageSize{960, 1440});
        CHECK(fitInside({800, 12000}, {1280, 0}) == ImageSize{800, 12000});  // a webtoon strip is never made bigger
        CHECK(fitInside({2000, 1500}, {1280, 720}) == ImageSize{960, 720});
        CHECK(fitInside({100, 100}, {0, 0}) == ImageSize{100, 100});
        CHECK(fitInside({10000, 1}, {100, 100}) == ImageSize{100, 1});  // never below 1 pixel
        CHECK(fitInside({0, 50}, {100, 100}) == ImageSize{0, 0});
        CHECK(rgbaBytes({1000, 1500}) == 6000000);
        CHECK(rgbaBytes({-1, 5}) == 0);
    }

    TEST_CASE("ImageGeometry: fitDecoded also stays within a byte budget") {
        const ImageSize fitted = fitDecoded({800, 12000}, {0, 0}, 16 * kMiB);
        CHECK(rgbaBytes(fitted) <= 16 * kMiB);
        CHECK(fitted.width < 800);
        CHECK(fitted.height == doctest::Approx(fitted.width * 15).epsilon(0.01));  // same shape
        CHECK(fitDecoded({100, 100}, {0, 0}, 1 * kMiB) == ImageSize{100, 100});
        CHECK(fitDecoded({100, 100}, {0, 0}, 4) == ImageSize{1, 1});
    }

    TEST_CASE("ImageGeometry: planDecode picks the JPEG scale and stays within the budget (table)") {
        struct Case {
            const char* what;
            ImageSize image, box;
            DecodeScaling scaling;
            std::uint64_t maxBytes, extra;
            bool ok;
            int denominator;
            ImageSize decoded, output;
        };
        const Case cases[] = {
            {"page to a 720p screen: 1/2, then shrink", {1000, 1500}, {1280, 720}, DecodeScaling::Dct, 128 * kMiB, 0, true, 2,
             {500, 750}, {480, 720}},
            {"page at 2x: full size, then shrink", {1000, 1500}, {2560, 1440}, DecodeScaling::Dct, 128 * kMiB, 0, true, 1,
             {1000, 1500}, {960, 1440}},
            {"exactly 1/8: no shrink step", {800, 800}, {100, 100}, DecodeScaling::Dct, 128 * kMiB, 0, true, 8,
             {100, 100}, {100, 100}},
            {"tight budget: a bigger reduction instead of failing", {1000, 1500}, {2560, 1440}, DecodeScaling::Dct, 4 * kMiB,
             0, true, 2, {500, 750}, {500, 750}},
            {"progressive coefficients count too", {1000, 1500}, {2560, 1440}, DecodeScaling::Dct, 10 * kMiB, 9 * kMiB, true,
             4, {250, 375}, {250, 375}},
            {"PNG: full size, then shrink", {1000, 1500}, {1280, 720}, DecodeScaling::None, 128 * kMiB, 0, true, 1,
             {1000, 1500}, {480, 720}},
            {"PNG too big at full size", {4000, 4000}, {1280, 720}, DecodeScaling::None, 32 * kMiB, 0, false, 1, {0, 0},
             {0, 0}},
            {"WebP strip, straight to the size", {800, 12000}, {2560, 0}, DecodeScaling::Any, 128 * kMiB, 0, true, 1,
             {800, 12000}, {800, 12000}},
            {"lossless WebP whose full size is over the budget", {5000, 5000}, {100, 100}, DecodeScaling::Any, 64 * kMiB,
             100 * kMiB, false, 1, {0, 0}, {0, 0}},
        };
        for (const Case& c : cases) {
            CAPTURE(c.what);
            const DecodePlan plan = planDecode(c.image, c.box, c.scaling, c.maxBytes, c.extra);
            CHECK(plan.ok == c.ok);
            if (!c.ok) {
                continue;
            }
            CHECK(plan.denominator == c.denominator);
            CHECK(plan.decoded == c.decoded);
            CHECK(plan.output == c.output);
            CHECK(plan.workBytes <= c.maxBytes);
        }
        // Slack (shrinkSlackPercent): a decode at most 15 % bigger than the target is kept as it is, no shrink step.
        const DecodePlan slack = planDecode({1000, 1500}, {2560, 1440}, DecodeScaling::Dct, 128 * kMiB, 0, 15);
        CHECK(slack.output == ImageSize{1000, 1500});  // 4 % over the box
        CHECK(slack.workBytes == rgbaBytes({1000, 1500}));
        CHECK(planDecode({1000, 1500}, {1280, 720}, DecodeScaling::Dct, 128 * kMiB, 0, 15).output == ImageSize{500, 750});
        CHECK(planDecode({1000, 1500}, {600, 900}, DecodeScaling::None, 128 * kMiB, 0, 15).output == ImageSize{600, 900});
        CHECK(planDecode({1000, 1500}, {950, 1425}, DecodeScaling::None, 128 * kMiB, 0, 5).output == ImageSize{950, 1425});
        CHECK(planDecode({1000, 1500}, {960, 1440}, DecodeScaling::None, 128 * kMiB, 0, 5).output == ImageSize{1000, 1500});
        // The squeezed PNG: full size fits, the shrunk copy gets what's left.
        const DecodePlan squeezed = planDecode({2000, 2000}, {1900, 1900}, DecodeScaling::None, 17 * kMiB);
        CHECK(squeezed.ok);
        CHECK(squeezed.workBytes <= 17 * kMiB);
        CHECK(squeezed.output.width < 1900);
        CHECK_FALSE(planDecode({0, 0}, {10, 10}, DecodeScaling::Dct, kMiB).ok);
    }

    TEST_CASE("ImageGeometry: planTiles cuts into equal tiles no bigger than the texture side") {
        const auto strip = planTiles({800, 12000}, 2048);
        REQUIRE(strip.size() == 6);
        for (std::size_t i = 0; i < strip.size(); ++i) {
            CHECK(strip[i].x == 0);
            CHECK(strip[i].y == static_cast<int>(i) * 2000);
            CHECK(strip[i].width == 800);
            CHECK(strip[i].height == 2000);
        }
        const auto wide = planTiles({4100, 1000}, 2048);  // three columns, the last a little narrower
        REQUIRE(wide.size() == 3);
        CHECK(wide[0].width == 1367);
        CHECK(wide[2].x == 2734);
        CHECK(wide[2].width == 1366);
        const auto grid = planTiles({3000, 3000}, 2048);
        REQUIRE(grid.size() == 4);
        CHECK(grid[1].x == 1500);
        CHECK(grid[2].y == 1500);
        CHECK(planTiles({480, 720}, 2048).size() == 1);
        CHECK(planTiles({0, 720}, 2048).empty());
        CHECK(planTiles({10, 10}, 0).empty());
        int area = 0;
        for (const TileRect& t : planTiles({2049, 4097}, 2048)) {
            CHECK(t.width <= 2048);
            CHECK(t.height <= 2048);
            area += t.width * t.height;
        }
        CHECK(area == 2049 * 4097);
    }

    TEST_CASE("ImageGeometry: tileWindow keeps the view plus N rows around it") {
        const auto strip = planTiles({800, 12000}, 2048);  // rows at 0, 2000, ..., 10000
        CHECK(tileWindow(strip, 4100, 4800, 0).first == 2);
        CHECK(tileWindow(strip, 4100, 4800, 0).last == 3);
        CHECK(tileWindow(strip, 3900, 4100, 0).first == 1);  // spans two rows
        CHECK(tileWindow(strip, 3900, 4100, 0).last == 3);
        CHECK(tileWindow(strip, 4100, 4800, 4).first == 0);  // clamped at the top
        CHECK(tileWindow(strip, 4100, 4800, 4).last == 6);
        CHECK(tileWindow(strip, 4100, 4800, 1).first == 1);
        CHECK(tileWindow(strip, 4100, 4800, 1).last == 4);
        CHECK(tileWindow(strip, -500, -100, 1).first == 0);  // above everything: the first row stands in
        CHECK(tileWindow(strip, -500, -100, 1).last == 2);
        CHECK(tileWindow(strip, 50000, 50100, 0).first == 5);  // below everything: the last row
        CHECK(tileWindow(strip, 50000, 50100, 0).last == 6);
        const auto grid = planTiles({3000, 3000}, 2048);  // two rows of two
        CHECK(tileWindow(grid, 0, 10, 0).first == 0);
        CHECK(tileWindow(grid, 0, 10, 0).last == 2);
        CHECK(tileWindow({}, 0, 10, 2).first == tileWindow({}, 0, 10, 2).last);
    }

    TEST_CASE("ByteLru: evicts the least recently used first, within the byte budget") {
        ByteLru<std::string, int> lru(100);
        CHECK(lru.put("a", 1, 40));
        CHECK(lru.put("b", 2, 40));
        REQUIRE(lru.get("a") != nullptr);  // a is now the most recently used
        std::vector<std::string> evicted;
        CHECK(lru.put("c", 3, 40, &evicted));
        CHECK(evicted == std::vector<std::string>{"b"});
        CHECK(lru.keysOldestFirst() == std::vector<std::string>{"a", "c"});
        CHECK(lru.bytes() == 80);
        CHECK(lru.peek("b") == nullptr);
        CHECK_FALSE(lru.put("huge", 4, 101));  // bigger than the whole budget: not kept
        CHECK(lru.size() == 2);
        CHECK(lru.put("a", 10, 10));  // replacing counts the new size
        CHECK(lru.bytes() == 50);
        CHECK(*lru.peek("a") == 10);
        CHECK(lru.keysOldestFirst() == std::vector<std::string>{"c", "a"});
        lru.setBudget(20, &evicted);
        CHECK(lru.keysOldestFirst() == std::vector<std::string>{"a"});
        CHECK(lru.erase("a"));
        CHECK_FALSE(lru.erase("a"));
        CHECK(lru.bytes() == 0);
        lru.put("x", 1, 5);
        lru.clear();
        CHECK(lru.size() == 0);
        CHECK(lru.bytes() == 0);
    }

    TEST_CASE("MemoryPressure: enters above pressurePercent, leaves below pressureRecoverPercent") {
        RuntimeProfile profile;  // 85 / 75 / 50
        MemoryPressure pressure(profile);
        const std::uint64_t total = 1000 * kMiB;
        CHECK(pressure.update({500 * kMiB, total}) == MemoryPressure::Change::None);
        CHECK_FALSE(pressure.underPressure());
        CHECK(pressure.update({860 * kMiB, total}) == MemoryPressure::Change::Entered);
        CHECK(pressure.underPressure());
        CHECK(pressure.usedPercent() == 86);
        CHECK(pressure.update({800 * kMiB, total}) == MemoryPressure::Change::None);  // between the two: stays
        CHECK(pressure.update({700 * kMiB, total}) == MemoryPressure::Change::Left);
        CHECK(pressure.update({0, 0}) == MemoryPressure::Change::None);  // nothing known: no change
        CHECK(pressure.update({5000 * kMiB, total}) == MemoryPressure::Change::Entered);  // used > total counts as 100 %
        CHECK(pressure.usedPercent() == 100);
        CHECK(pressure.shrinkPercent() == 50);
    }

    TEST_CASE("TestConfig: imageBench") {
        const auto good = testconfig::parse(R"({"imageBench": {"paths": ["/a/1", "/b/2"], "repeat": 5}})");
        CHECK(good.problems.empty());
        CHECK(good.benchPaths == std::vector<std::string>{"/a/1", "/b/2"});
        CHECK(good.benchRepeat == 5);
        for (const char* bad : {R"({"imageBench": 1})", R"({"imageBench": {"paths": [], "repeat": 1}})",
                                R"({"imageBench": {"paths": ["x"], "repeat": 1}})",
                                R"({"imageBench": {"paths": ["/a"], "repeat": 21}})",
                                R"({"imageBench": {"paths": ["/a b"], "repeat": 1}})"}) {
            CAPTURE(bad);
            const auto config = testconfig::parse(bad);
            CHECK(config.benchRepeat == 0);
            CHECK(config.benchPaths.empty());
            CHECK(config.problems.size() == 1);
        }
    }

    TEST_CASE("TestEvents: image and mem_pressure lines") {
        testevents::ImageEvent event;
        event.url = "http://x/p/1";
        event.source = "net";
        event.format = "jpeg";
        event.width = 480;
        event.height = 720;
        event.decodeMs = 31;
        event.bytes = 1382400;
        const std::string line = testevents::image(1700, event);
        CHECK(line.rfind(R"({"t":1700,"ev":"image","url":"http://x/p/1","source":"net","error":"","format":"jpeg","w":480,)", 0) ==
              0);
        CHECK(line.find(R"("decodeMs":31,"bytes":1382400)") != std::string::npos);
        CHECK(testevents::memPressure(9600, 87, "shrink_caches") ==
              "{\"t\":9600,\"ev\":\"mem_pressure\",\"usedPct\":87,\"action\":\"shrink_caches\"}\n");
    }
}
