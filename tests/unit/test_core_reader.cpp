// core's paged-reader rules (WP-11): turning by side and direction, tap zones, swipes, fit sizes and decode boxes, where a page
// sits and how far it moves, the prefetch window, the progress sync timing, and the per-manga reader settings in config.json.

#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "core/AppConfig.hpp"
#include "core/ProgressSync.hpp"
#include "core/ReaderMath.hpp"
#include "core/RuntimeProfile.hpp"

using namespace snx::core;

namespace {

bool near(PointF a, PointF b) {
    return a.x == doctest::Approx(b.x) && a.y == doctest::Approx(b.y);
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("ReaderMath: a side turns forward or back depending on the reading direction") {
        CHECK(pageStep(Side::Right, ReadingDirection::LeftToRight) == 1);
        CHECK(pageStep(Side::Left, ReadingDirection::LeftToRight) == -1);
        CHECK(pageStep(Side::Left, ReadingDirection::RightToLeft) == 1);  // mirrored
        CHECK(pageStep(Side::Right, ReadingDirection::RightToLeft) == -1);
        CHECK(pageStep(Side::Right, ReadingDirection::Vertical) == 1);  // like left to right until WP-13
    }

    TEST_CASE("ReaderMath: tap zones are the outer 30 % on each side") {
        CHECK(tapArea(0, 1280) == TapArea::Left);
        CHECK(tapArea(380, 1280) == TapArea::Left);
        CHECK(tapArea(388, 1280) == TapArea::Middle);
        CHECK(tapArea(640, 1280) == TapArea::Middle);
        CHECK(tapArea(892, 1280) == TapArea::Middle);
        CHECK(tapArea(900, 1280) == TapArea::Right);
        CHECK(tapArea(1279, 1280) == TapArea::Right);
        CHECK(tapArea(10, 0) == TapArea::Middle);
    }

    TEST_CASE("ReaderMath: a swipe turns only when long enough and mostly sideways") {
        CHECK(swipeSide(-200, 10, 80) == Side::Right);  // finger to the left: show what's on the right
        CHECK(swipeSide(200, -30, 80) == Side::Left);
        CHECK_FALSE(swipeSide(60, 0, 80).has_value());     // too short: a tap or a nudge
        CHECK_FALSE(swipeSide(150, 120, 80).has_value());  // too diagonal
        CHECK_FALSE(swipeSide(10, 300, 80).has_value());   // up/down scrolls the page
    }

    TEST_CASE("ReaderMath: fit sizes for a manga page, a spread and a webtoon strip on 1280 x 720") {
        const SizeF screen{1280, 720};
        const SizeF page = fitPage({1000, 1500}, screen, PageFit::Screen);
        CHECK(page.width == doctest::Approx(480));
        CHECK(page.height == doctest::Approx(720));
        const SizeF wide = fitPage({1000, 1500}, screen, PageFit::Width);
        CHECK(wide.width == doctest::Approx(1280));
        CHECK(wide.height == doctest::Approx(1920));
        const SizeF tall = fitPage({1000, 1500}, screen, PageFit::Height);
        CHECK(tall.height == doctest::Approx(720));
        const SizeF original = fitPage({1000, 1500}, screen, PageFit::Original);
        CHECK(original.width == doctest::Approx(1000));
        const SizeF spread = fitPage({2000, 1500}, screen, PageFit::Screen);
        CHECK(spread.width == doctest::Approx(960));
        CHECK(spread.height == doctest::Approx(720));
        const SizeF panorama = fitPage({3000, 1000}, screen, PageFit::Height);
        CHECK(panorama.width == doctest::Approx(2160));  // wider than the screen: it can be moved sideways
        const SizeF strip = fitPage({800, 12000}, screen, PageFit::Width);
        CHECK(strip.height == doctest::Approx(19200));
        CHECK(fitPage({0, 10}, screen, PageFit::Screen).width == 0);
        CHECK(fitPage({10, 10}, {0, 0}, PageFit::Screen).height == 0);
    }

    TEST_CASE("ReaderMath: decode boxes follow the fit") {
        const ImageSize screen{1280, 720};
        CHECK(decodeBoxFor(PageFit::Screen, screen) == ImageSize{1280, 720});
        CHECK(decodeBoxFor(PageFit::Width, screen) == ImageSize{1280, 0});
        CHECK(decodeBoxFor(PageFit::Height, screen) == ImageSize{0, 720});
        CHECK(decodeBoxFor(PageFit::Original, screen) == ImageSize{0, 0});
    }

    TEST_CASE("ReaderMath: spreads are more than 1.2 times as wide as tall") {
        CHECK(isSpread({2000, 1500}));
        CHECK_FALSE(isSpread({1000, 1500}));
        CHECK_FALSE(isSpread({1200, 1000}));
        CHECK(isSpread({1201, 1000}));
        CHECK_FALSE(isSpread({0, 0}));
    }

    TEST_CASE("ReaderMath: a page is centred where it fits and can't move past its edges") {
        const SizeF screen{1280, 720};
        const SizeF small{480, 720};
        CHECK(near(clampPosition({-500, 300}, small, screen), {400, 0}));
        const SizeF tall{1280, 1920};
        CHECK(near(clampPosition({0, 100}, tall, screen), {0, 0}));
        CHECK(near(clampPosition({0, -5000}, tall, screen), {0, -1200}));
        CHECK(near(clampPosition({0, -600}, tall, screen), {0, -600}));
        const SizeF wide{2160, 720};
        CHECK(near(clampPosition({-2000, 0}, wide, screen), {-880, 0}));
    }

    TEST_CASE("ReaderMath: pages start at the top, sideways at the reading direction's start") {
        const SizeF screen{1280, 720};
        const SizeF wide{2160, 1000};
        CHECK(near(startPosition(wide, screen, ReadingDirection::LeftToRight), {0, 0}));
        CHECK(near(startPosition(wide, screen, ReadingDirection::RightToLeft), {-880, 0}));
        CHECK(near(startPosition({480, 720}, screen, ReadingDirection::RightToLeft), {400, 0}));
    }

    TEST_CASE("ReaderMath: canMoveToward says whether more of the page lies beyond a side") {
        const SizeF screen{1280, 720};
        const SizeF wide{2160, 720};
        CHECK(canMoveToward({0, 0}, wide, screen, Side::Right));
        CHECK_FALSE(canMoveToward({0, 0}, wide, screen, Side::Left));
        CHECK(canMoveToward({-880, 0}, wide, screen, Side::Left));
        CHECK_FALSE(canMoveToward({-880, 0}, wide, screen, Side::Right));
        CHECK(canMoveToward({-400, 0}, wide, screen, Side::Left));
        CHECK(canMoveToward({-400, 0}, wide, screen, Side::Right));
        CHECK_FALSE(canMoveToward({400, 0}, {480, 720}, screen, Side::Right));  // fits: never
    }

    TEST_CASE("ReaderMath: the prefetch window, current first, then ahead, then behind") {
        CHECK(prefetchOrder(4, 10, 1, 3) == std::vector<int>{4, 5, 6, 7, 3});
        CHECK(prefetchOrder(0, 10, 1, 3) == std::vector<int>{0, 1, 2, 3});
        CHECK(prefetchOrder(8, 10, 1, 3) == std::vector<int>{8, 9, 7});
        CHECK(prefetchOrder(0, 1, 1, 3) == std::vector<int>{0});
        CHECK(prefetchOrder(3, 10, 0, 0) == std::vector<int>{3});  // safe mode: prefetch off
        CHECK(prefetchOrder(0, 0, 1, 3).empty());
        CHECK(nearChapterEnd(7, 10, 3));
        CHECK_FALSE(nearChapterEnd(6, 10, 3));
        CHECK(nearChapterEnd(9, 10, 1));
        CHECK_FALSE(nearChapterEnd(9, 10, 0));
    }

    TEST_CASE("ProgressSync: at most one update per interval, counted from opening") {
        ProgressSync sync(45, 0, false, 5000, 1000);
        CHECK_FALSE(sync.pending());
        sync.shown(0, false);  // the page it opened at: nothing to send
        CHECK_FALSE(sync.pending());
        sync.shown(1, false);
        CHECK(sync.pending());
        CHECK_FALSE(sync.due(3000).has_value());  // 2 s after opening
        const auto first = sync.due(6000);
        REQUIRE(first.has_value());
        CHECK(first->chapterId == 45);
        CHECK(first->lastPageRead == 1);
        CHECK_FALSE(first->isRead);
        sync.sending(*first, 6000);
        CHECK(sync.inFlight());
        sync.shown(2, false);
        CHECK_FALSE(sync.due(20000).has_value());  // one at a time
        CHECK_FALSE(sync.dueNow().has_value());
        sync.saved(*first);
        CHECK_FALSE(sync.due(9000).has_value());  // 3 s after the last send
        const auto second = sync.due(11000);
        REQUIRE(second.has_value());
        CHECK(second->lastPageRead == 2);
        sync.sending(*second, 11000);
        sync.saved(*second);
        CHECK_FALSE(sync.pending());
    }

    TEST_CASE("ProgressSync: the last page marks the chapter read at once, and only once") {
        ProgressSync sync(7, 3, false, 5000, 0);
        sync.shown(9, true);
        CHECK(sync.read());
        const auto read = sync.due(100);  // doesn't wait for the interval
        REQUIRE(read.has_value());
        CHECK(read->isRead);
        CHECK(read->lastPageRead == 9);
        sync.sending(*read, 100);
        sync.saved(*read);
        sync.shown(8, false);  // back one page: lastPageRead follows, isRead isn't sent again (and never as false)
        const auto back = sync.due(6000);
        REQUIRE(back.has_value());
        CHECK_FALSE(back->isRead);
        CHECK(back->lastPageRead == 8);
        CHECK(sync.read());
    }

    TEST_CASE("ProgressSync: a failed update is tried again after the interval; dueNow doesn't wait") {
        ProgressSync sync(7, 0, false, 5000, 0);
        sync.shown(4, false);
        const auto first = sync.dueNow();
        REQUIRE(first.has_value());
        sync.sending(*first, 100);
        sync.failed();
        CHECK(sync.pending());
        CHECK_FALSE(sync.due(2000).has_value());
        CHECK(sync.due(5100).has_value());
        CHECK(sync.dueNow().has_value());
    }

    TEST_CASE("ProgressSync: a chapter that is read already never sends isRead") {
        ProgressSync sync(7, 9, true, 5000, 0);
        sync.shown(9, true);
        CHECK_FALSE(sync.pending());
        sync.shown(0, false);
        const auto progress = sync.dueNow();
        REQUIRE(progress.has_value());
        CHECK_FALSE(progress->isRead);
        CHECK(progress->lastPageRead == 0);
    }

    TEST_CASE("AppConfig: per-manga reader settings, defaults, most recent last, at most max") {
        ReaderSettings reader;
        reader.direction = ReadingDirection::RightToLeft;
        reader.fit = PageFit::Screen;
        MangaReaderSettings defaults = readerSettingsFor(reader, 5);
        CHECK(defaults.mangaId == 5);
        CHECK(defaults.direction == ReadingDirection::RightToLeft);
        CHECK(defaults.fit == PageFit::Screen);

        rememberReaderSettings(reader, {5, ReadingDirection::LeftToRight, PageFit::Width}, 3);
        rememberReaderSettings(reader, {6, ReadingDirection::LeftToRight, PageFit::Screen}, 3);
        rememberReaderSettings(reader, {7, ReadingDirection::RightToLeft, PageFit::Height}, 3);
        CHECK(readerSettingsFor(reader, 5).fit == PageFit::Width);
        rememberReaderSettings(reader, {5, ReadingDirection::LeftToRight, PageFit::Height}, 3);  // moves to the end
        REQUIRE(reader.manga.size() == 3);
        CHECK(reader.manga.back().mangaId == 5);
        CHECK(reader.manga.front().mangaId == 6);
        rememberReaderSettings(reader, {8, ReadingDirection::LeftToRight, PageFit::Screen}, 3);  // 6 goes
        REQUIRE(reader.manga.size() == 3);
        CHECK(reader.manga.front().mangaId == 7);
        CHECK(readerSettingsFor(reader, 6).direction == ReadingDirection::RightToLeft);  // the default again
        CHECK(RuntimeProfile{}.readerMangaSettingsMax > 0);
    }

    TEST_CASE("AppConfig: reader.manga entries that can't be used are skipped, the rest kept") {
        const std::string text = R"({"schemaVersion": 1, "reader": {"direction": "ltr", "fit": "width",
            "background": "purple", "pageNumber": "yes",
            "manga": [{"id": 3, "direction": "rtl", "fit": "height"}, {"direction": "rtl"}, 7,
                      {"id": 4, "direction": "sideways", "fit": "screen"}, {"id": 3, "direction": "ltr", "fit": "screen"}]}})";
        const ConfigLoad load = loadAppConfig(text);
        const ReaderSettings& reader = load.config.reader;
        CHECK(reader.background == ReaderBackground::Black);  // unknown: the default
        CHECK(reader.showPageNumber);
        REQUIRE(reader.manga.size() == 2);
        CHECK(reader.manga[0].mangaId == 4);
        CHECK(reader.manga[0].direction == ReadingDirection::LeftToRight);  // unknown: the reader's default
        CHECK(reader.manga[1].mangaId == 3);  // listed twice: the last one wins
        CHECK(reader.manga[1].direction == ReadingDirection::LeftToRight);
        CHECK(reader.manga[1].fit == PageFit::Screen);
        bool skippedNoted = false;
        for (const std::string& problem : load.problems) {
            skippedNoted = skippedNoted || problem.find("2 entries of reader.manga") != std::string::npos;
        }
        CHECK(skippedNoted);
        CHECK(loadAppConfig(R"({"schemaVersion": 1, "reader": {"manga": "x"}})").problems.size() == 1);
    }
}
