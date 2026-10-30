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

    TEST_CASE("ReaderMath: a quick touch that slid a little is still a tap") {
        CHECK(isTap(-20, 4, 120, 40, 300));   // a quick finger
        CHECK(isTap(0, 40, 300, 40, 300));    // right at both limits
        CHECK_FALSE(isTap(30, 30, 120, 40, 300));  // 42 units: a short drag
        CHECK_FALSE(isTap(10, 0, 301, 40, 300));   // held too long: a slow nudge of the page
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
        ProgressSync sync(45, 0, false, 5000, 60000, 1000);
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
        ProgressSync sync(7, 3, false, 5000, 60000, 0);
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
        ProgressSync sync(7, 0, false, 5000, 60000, 0);
        sync.shown(4, false);
        const auto first = sync.dueNow();
        REQUIRE(first.has_value());
        sync.sending(*first, 100);
        sync.failed(100);
        CHECK(sync.pending());
        CHECK_FALSE(sync.due(2000).has_value());
        CHECK(sync.due(5100).has_value());
        CHECK(sync.dueNow().has_value());
    }

    TEST_CASE("ProgressSync: each failure in a row doubles the wait, up to the cap; isRead waits too; a save resets it") {
        ProgressSync sync(7, 0, false, 5000, 12000, 0);
        sync.shown(9, true);  // the last page: isRead, which normally goes out at once
        std::int64_t now = 0;
        for (const int waitMs : {5000, 10000, 12000, 12000}) {
            const auto progress = sync.due(now);
            REQUIRE(progress.has_value());
            CHECK(progress->isRead);
            sync.sending(*progress, now);
            now += 5000;  // the request timed out
            sync.failed(now);
            CHECK_FALSE(sync.due(now).has_value());  // not again in the next frame
            CHECK_FALSE(sync.due(now + waitMs - 1).has_value());
            now += waitMs;
        }
        const auto progress = sync.due(now);
        REQUIRE(progress.has_value());
        sync.sending(*progress, now);
        sync.saved(*progress);
        sync.shown(3, false);
        CHECK(sync.due(now + 5000).has_value());  // back to the plain interval
    }

    TEST_CASE("ProgressSync: a chapter that is read already never sends isRead") {
        ProgressSync sync(7, 9, true, 5000, 60000, 0);
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

    TEST_CASE("AppConfig: reader.orientation and lastPortrait, old files without them, a round trip") {
        const ConfigLoad old = loadAppConfig(R"({"schemaVersion": 1, "reader": {"fit": "width"}})");
        CHECK(old.problems.empty());
        CHECK(old.config.reader.orientation == ReaderOrientation::Landscape);
        CHECK(old.config.reader.lastPortrait == ReaderOrientation::PortraitLeft);

        AppConfig config;
        config.reader.orientation = ReaderOrientation::PortraitRight;
        config.reader.lastPortrait = ReaderOrientation::PortraitRight;
        const std::string text = saveAppConfig(config);
        CHECK(text.find(R"("orientation": "portrait-right")") != std::string::npos);
        const ConfigLoad back = loadAppConfig(text);
        CHECK(back.config.reader.orientation == ReaderOrientation::PortraitRight);
        CHECK(back.config.reader.lastPortrait == ReaderOrientation::PortraitRight);

        // lastPortrait is never landscape; an unknown name keeps the default.
        const ConfigLoad bad = loadAppConfig(
            R"({"schemaVersion": 1, "reader": {"orientation": "upside-down", "lastPortrait": "landscape"}})");
        CHECK(bad.config.reader.orientation == ReaderOrientation::Landscape);
        CHECK(bad.config.reader.lastPortrait == ReaderOrientation::PortraitLeft);
        CHECK(bad.problems.size() == 2);
        CHECK(readerOrientationName(ReaderOrientation::PortraitLeft) == "portrait-left");
        CHECK(readerOrientationFromName("portrait-right") == ReaderOrientation::PortraitRight);
    }

    TEST_CASE("ReaderMath: orientation in effect, quick rotate, the reader's screen") {
        CHECK(effectiveOrientation(ReaderOrientation::PortraitLeft, true) == ReaderOrientation::PortraitLeft);
        CHECK(effectiveOrientation(ReaderOrientation::PortraitLeft, false) == ReaderOrientation::Landscape);  // docked
        CHECK(quickRotate(ReaderOrientation::Landscape, ReaderOrientation::PortraitRight) == ReaderOrientation::PortraitRight);
        CHECK(quickRotate(ReaderOrientation::Landscape, ReaderOrientation::Landscape) == ReaderOrientation::PortraitLeft);
        CHECK(quickRotate(ReaderOrientation::PortraitLeft, ReaderOrientation::PortraitLeft) == ReaderOrientation::Landscape);
        CHECK(quickRotate(ReaderOrientation::PortraitRight, ReaderOrientation::PortraitLeft) == ReaderOrientation::Landscape);
        const SizeF portrait = readerScreen(ReaderOrientation::PortraitRight, SizeF{1280, 720});
        CHECK(portrait.width == 720);
        CHECK(portrait.height == 1280);
        CHECK(readerScreen(ReaderOrientation::Landscape, SizeF{1280, 720}).width == 1280);
        CHECK(readerScreen(ReaderOrientation::PortraitLeft, ImageSize{1920, 1080}) == ImageSize{1080, 1920});
        CHECK(readerScreen(ReaderOrientation::Landscape, ImageSize{1280, 720}) == ImageSize{1280, 720});
    }

    TEST_CASE("ReaderMath: points and moves between the real screen and the reader's, both portrait ways") {
        const SizeF screen{1280, 720};
        // Turned left: the reader's top-left corner is the real top-right one, its bottom-left the real top-left.
        CHECK(near(readerToScreen(ReaderOrientation::PortraitLeft, {0, 0}, screen), {1280, 0}));
        CHECK(near(readerToScreen(ReaderOrientation::PortraitLeft, {0, 1280}, screen), {0, 0}));
        CHECK(near(readerToScreen(ReaderOrientation::PortraitLeft, {720, 0}, screen), {1280, 720}));
        // Turned right: the reader's top-left corner is the real bottom-left one.
        CHECK(near(readerToScreen(ReaderOrientation::PortraitRight, {0, 0}, screen), {0, 720}));
        CHECK(near(readerToScreen(ReaderOrientation::PortraitRight, {720, 0}, screen), {0, 0}));
        CHECK(near(readerToScreen(ReaderOrientation::PortraitRight, {720, 1280}, screen), {1280, 0}));
        CHECK(near(readerToScreen(ReaderOrientation::Landscape, {100, 200}, screen), {100, 200}));
        // And back, for a few points.
        for (const ReaderOrientation o :
             {ReaderOrientation::Landscape, ReaderOrientation::PortraitLeft, ReaderOrientation::PortraitRight}) {
            for (const PointF p : {PointF{0, 0}, PointF{100, 900}, PointF{719, 1279}, PointF{360, 640}}) {
                CHECK(near(screenToReader(o, readerToScreen(o, p, screen), screen), p));
            }
        }
        // The middle stays the middle.
        CHECK(near(screenToReader(ReaderOrientation::PortraitLeft, {640, 360}, screen), {360, 640}));
        // A move: turned left, the finger going to the real left goes down the reader's screen.
        CHECK(near(screenMoveToReader(ReaderOrientation::PortraitLeft, {-200, 0}), {0, 200}));
        CHECK(near(screenMoveToReader(ReaderOrientation::PortraitLeft, {0, 300}), {300, 0}));
        CHECK(near(screenMoveToReader(ReaderOrientation::PortraitRight, {-200, 0}), {0, -200}));
        CHECK(near(screenMoveToReader(ReaderOrientation::PortraitRight, {0, 300}), {-300, 0}));
        CHECK(near(screenMoveToReader(ReaderOrientation::Landscape, {5, 6}), {5, 6}));
        // A move equals the difference of its mapped end points.
        const PointF a{300, 100};
        const PointF b{500, 400};
        for (const ReaderOrientation o : {ReaderOrientation::PortraitLeft, ReaderOrientation::PortraitRight}) {
            const PointF ra = screenToReader(o, a, screen);
            const PointF rb = screenToReader(o, b, screen);
            CHECK(near(screenMoveToReader(o, {b.x - a.x, b.y - a.y}), {rb.x - ra.x, rb.y - ra.y}));
        }
        // The transform is the one readerToScreen uses (NanoVG gets these numbers).
        const Transform t = readerTransform(ReaderOrientation::PortraitLeft, screen);
        CHECK(t.e == 1280);
        CHECK(t.c == -1);
    }

    TEST_CASE("ReaderMath: the D-pad turns with the screen") {
        CHECK(readerArrow(ReaderOrientation::Landscape, Arrow::Right) == Arrow::Right);
        CHECK(readerArrow(ReaderOrientation::Landscape, Arrow::Up) == Arrow::Up);
        // Turned left: the D-pad's right button points up, its up button to the left.
        CHECK(readerArrow(ReaderOrientation::PortraitLeft, Arrow::Right) == Arrow::Up);
        CHECK(readerArrow(ReaderOrientation::PortraitLeft, Arrow::Up) == Arrow::Left);
        CHECK(readerArrow(ReaderOrientation::PortraitLeft, Arrow::Left) == Arrow::Down);
        CHECK(readerArrow(ReaderOrientation::PortraitLeft, Arrow::Down) == Arrow::Right);
        // Turned right: the right button points down, the up button to the right.
        CHECK(readerArrow(ReaderOrientation::PortraitRight, Arrow::Right) == Arrow::Down);
        CHECK(readerArrow(ReaderOrientation::PortraitRight, Arrow::Up) == Arrow::Right);
        CHECK(readerArrow(ReaderOrientation::PortraitRight, Arrow::Left) == Arrow::Up);
        CHECK(readerArrow(ReaderOrientation::PortraitRight, Arrow::Down) == Arrow::Left);
    }

    TEST_CASE("ReaderMath: fits and decode boxes on the portrait screen") {
        const SizeF portrait = readerScreen(ReaderOrientation::PortraitLeft, SizeF{1280, 720});
        // A typical page (1 : 1.45) fills the width: 720 x 1044, about twice the area of 497 x 720 in landscape.
        const SizeF page = fitPage({1000, 1450}, portrait, PageFit::Screen);
        CHECK(page.width == doctest::Approx(720));
        CHECK(page.height == doctest::Approx(1044));
        const SizeF landscape = fitPage({1000, 1450}, {1280, 720}, PageFit::Screen);
        CHECK(page.width * page.height > 2 * landscape.width * landscape.height);
        // A spread is shown whole, so small: 720 x 540.
        const SizeF spread = fitPage({2000, 1500}, portrait, PageFit::Screen);
        CHECK(spread.width == doctest::Approx(720));
        CHECK(spread.height == doctest::Approx(540));
        CHECK(near(startPosition(spread, portrait, ReadingDirection::RightToLeft), {0, 370}));
        // Fit to width: taller than the screen, it moves up and down.
        const SizeF wide = fitPage({1000, 2000}, portrait, PageFit::Width);
        CHECK(wide.height == doctest::Approx(1440));
        CHECK(near(clampPosition({0, -9000}, wide, portrait), {0, -160}));
        // Pages decode at their portrait size.
        const ImageSize real = readerScreen(ReaderOrientation::PortraitRight, ImageSize{1280, 720});
        CHECK(decodeBoxFor(PageFit::Screen, real) == ImageSize{720, 1280});
        CHECK(decodeBoxFor(PageFit::Width, real) == ImageSize{720, 0});
        CHECK(fitInside({1000, 1450}, decodeBoxFor(PageFit::Screen, real)) == ImageSize{720, 1044});
    }
}
