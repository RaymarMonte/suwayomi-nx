// core's webtoon strip rules (WP-13b): the strip's width and decode boxes, the layout and its anchor (what is under the middle
// line stays still), the tile window across pages, the prefetch window by screens, flicks and scrolling, tap zones, the spot
// note, the "by itself" rules, the new config keys, ProgressSync's spot, and the UpdateStripProgress request and answer.

#include <cmath>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "core/AppConfig.hpp"
#include "core/ProgressSync.hpp"
#include "core/StripMath.hpp"
#include "core/graphql/Operations.hpp"
#include "core/models/FromJson.hpp"

using namespace snx::core;

namespace {

StripItem band(double height = 240) {
    return StripItem{StripItemKind::Band, 0, -1, height};
}
StripItem page(int chapterId, int index, double height) {
    return StripItem{StripItemKind::Page, chapterId, index, height};
}

// Gamma's chapter 1 in landscape at half width: a band, three 640 x 9600 strips, a band. 720 units on screen.
StripLayout gammaLayout() {
    StripLayout layout;
    layout.setViewHeight(720);
    layout.insert(0, {band(), page(1, 0, 9600), page(1, 1, 9600), page(1, 2, 9600), band()});
    return layout;
}

// The strip y under the screen's middle line.
double middleY(const StripLayout& layout) {
    return layout.top() + layout.viewHeight() / 2;
}

GqlResponse answer(const char* body) {
    auto r = parseGqlResponse(body);
    REQUIRE(r.ok());
    return r.value();
}

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("strip width: half / three quarters / full of the landscape screen, always full in portrait") {
        CHECK(stripWidth(StripWidth::Half, ReaderOrientation::Landscape, 1280) == doctest::Approx(640));
        CHECK(stripWidth(StripWidth::ThreeQuarters, ReaderOrientation::Landscape, 1280) == doctest::Approx(960));
        CHECK(stripWidth(StripWidth::Full, ReaderOrientation::Landscape, 1280) == doctest::Approx(1280));
        for (const StripWidth w : {StripWidth::Half, StripWidth::ThreeQuarters, StripWidth::Full}) {
            CHECK(stripWidth(w, ReaderOrientation::PortraitLeft, 720) == doctest::Approx(720));
            CHECK(stripWidth(w, ReaderOrientation::PortraitRight, 720) == doctest::Approx(720));
        }
        // The decode box: the strip's width in real pixels, no height limit (docked: 1920 wide).
        CHECK(stripDecodeBox(StripWidth::Half, ReaderOrientation::Landscape, {1280, 720}) == ImageSize{640, 0});
        CHECK(stripDecodeBox(StripWidth::Half, ReaderOrientation::Landscape, {1920, 1080}) == ImageSize{960, 0});
        CHECK(stripDecodeBox(StripWidth::Full, ReaderOrientation::PortraitLeft, {720, 1280}) == ImageSize{720, 0});
        // An 800-wide strip decodes at 800 in portrait (the loader's 15 % slack) and 640 in landscape.
        CHECK(fitInside({800, 12000}, stripDecodeBox(StripWidth::Half, ReaderOrientation::Landscape, {1280, 720})) ==
              ImageSize{640, 9600});
        // Zoomed: the strip's width x the zoom's step, capped at maxDecodeScale x the strip's width.
        CHECK(stripZoomDecodeBox(StripWidth::Half, ReaderOrientation::Landscape, {1280, 720}, 1, 4, 4) == ImageSize{640, 0});
        CHECK(stripZoomDecodeBox(StripWidth::Half, ReaderOrientation::Landscape, {1280, 720}, 1.7f, 4, 4) ==
              ImageSize{1280, 0});
        CHECK(stripZoomDecodeBox(StripWidth::Full, ReaderOrientation::PortraitLeft, {720, 1280}, 3, 4, 2) ==
              ImageSize{1440, 0});
    }

    TEST_CASE("strip page heights, the estimate for unknown pages, and the band") {
        CHECK(stripPageHeight({800, 12000}, 640) == doctest::Approx(9600));
        CHECK(stripPageHeight({1000, 1500}, 720) == doctest::Approx(1080));
        CHECK(stripPageHeight({0, 0}, 640) == doctest::Approx(960));  // unknown: 1.5 x the width
        CHECK(estimatedPageHeight(std::nullopt, 640) == doctest::Approx(960));
        CHECK(estimatedPageHeight(ImageSize{800, 12000}, 720) == doctest::Approx(10800));
        CHECK(stripBandHeight(720) == doctest::Approx(240));
        CHECK(stripBandHeight(1280) == doctest::Approx(1280.0 / 3));
    }

    TEST_CASE("StripLayout: items, tops, itemAt at the edges, overlapping") {
        const StripLayout layout = gammaLayout();
        REQUIRE(layout.size() == 5);
        CHECK(layout.contentHeight() == doctest::Approx(240 + 3 * 9600 + 240));
        CHECK(layout.itemTop(1) == doctest::Approx(240));
        CHECK(layout.itemBottom(1) == doctest::Approx(9840));
        CHECK(layout.itemAt(-5) == 0);
        CHECK(layout.itemAt(239.9) == 0);
        CHECK(layout.itemAt(240) == 1);  // an edge belongs to the item below it
        CHECK(layout.itemAt(9840) == 2);
        CHECK(layout.itemAt(1e9) == 4);
        CHECK(layout.overlapping(0, 720) == std::pair<int, int>{0, 2});
        CHECK(layout.overlapping(9500, 9840) == std::pair<int, int>{1, 2});  // ends exactly at the seam
        CHECK(layout.overlapping(9500, 9841) == std::pair<int, int>{1, 3});
        CHECK(layout.overlapping(-100, 0) == std::pair<int, int>{0, 0});
        CHECK(layout.find(1, 2) == 3);
        CHECK(layout.find(2, 0) == -1);
    }

    TEST_CASE("StripLayout: clampTop at both ends, and a strip shorter than the screen sits at the top") {
        StripLayout layout = gammaLayout();
        CHECK(layout.clampTop(-50) == 0);
        CHECK(layout.clampTop(1e9) == doctest::Approx(layout.contentHeight() - 720));
        CHECK(layout.scrollTo(1e9) == doctest::Approx(layout.contentHeight() - 720));
        CHECK(layout.scrollBy(100) == doctest::Approx(0));  // no gap below the end band
        CHECK(layout.scrollBy(-100) == doctest::Approx(-100));

        StripLayout shortStrip;
        shortStrip.setViewHeight(720);
        shortStrip.insert(0, {band(), page(1, 0, 200), band()});
        CHECK(shortStrip.contentHeight() < 720);
        CHECK(shortStrip.scrollTo(300) == doctest::Approx(0));
        CHECK(shortStrip.top() == doctest::Approx(0));
        const StripPlace middle = shortStrip.middle();  // y 360: the page [240, 440)
        CHECK(middle.item == 1);
        CHECK(middle.share == doctest::Approx(0.6));
        // A line past the end: the last item, at its bottom.
        CHECK(shortStrip.placeAt(700).item == 2);
        CHECK(shortStrip.placeAt(700).share == doctest::Approx(1));
    }

    TEST_CASE("StripLayout: middle() and topFor() round trip (spot -> top -> spot)") {
        StripLayout layout = gammaLayout();
        for (const double share : {0.0, 0.1, 0.4375, 0.9}) {
            const StripPlace spot{2, share};
            layout.scrollTo(layout.topFor(spot, layout.viewHeight() / 2));
            const StripPlace back = layout.middle();
            CHECK(back.item == 2);
            CHECK(back.share == doctest::Approx(share));
        }
        // A page's top at the screen's top.
        layout.scrollTo(layout.topFor({3, 0}, 0));
        CHECK(layout.top() == doctest::Approx(layout.itemTop(3)));
        CHECK(layout.placeAt(0).item == 3);
        CHECK(layout.placeAt(0).share == doctest::Approx(0));
    }

    TEST_CASE("StripLayout: a height change above the middle line keeps the view still, below it moves nothing") {
        StripLayout layout = gammaLayout();
        layout.scrollTo(layout.topFor({2, 0.5}, 360));  // the middle of page 1
        const double y = middleY(layout) - layout.itemTop(2);
        const double top = layout.top();
        // Page 0 (above) grows by 1000: everything moves down the strip by as much, the screen with it.
        layout.setHeight(1, 10600);
        CHECK(layout.top() == doctest::Approx(top + 1000));
        CHECK(middleY(layout) - layout.itemTop(2) == doctest::Approx(y));
        // Page 2 (below) shrinks: nothing moves.
        layout.setHeight(3, 2000);
        CHECK(layout.top() == doctest::Approx(top + 1000));
        // The page under the middle line itself changes: its share stays under the line.
        const StripPlace before = layout.middle();
        layout.setHeight(2, 4800);
        CHECK(layout.middle().item == before.item);
        CHECK(layout.middle().share == doctest::Approx(before.share));
    }

    TEST_CASE("StripLayout: chapters joined above and below, and cut out, keep the view still") {
        StripLayout layout = gammaLayout();
        layout.scrollTo(layout.topFor({1, 0.25}, 360));
        const StripPlace reading = layout.middle();
        const double top = layout.top();
        // The previous chapter joins above (its band and pages before the old top band).
        layout.insert(0, {band(), page(7, 0, 1000), page(7, 1, 1500)});
        CHECK(layout.top() == doctest::Approx(top + 240 + 2500));
        CHECK(layout.middle().item == reading.item + 3);
        CHECK(layout.middle().share == doctest::Approx(reading.share));
        // The next chapter joins below the end band: nothing moves.
        const double afterAbove = layout.top();
        layout.insert(layout.size(), {page(2, 0, 9600), band()});
        CHECK(layout.top() == doctest::Approx(afterAbove));
        // The previous chapter is cut out again (its band and pages): the view stays.
        layout.erase(0, 3);
        CHECK(layout.top() == doctest::Approx(top));
        CHECK(layout.middle().item == reading.item);
        CHECK(layout.middle().share == doctest::Approx(reading.share));
    }

    TEST_CASE("StripLayout: a pin holds a page's top at the screen's top until a scroll") {
        StripLayout layout;
        layout.setViewHeight(720);
        // Opening: pages not known yet, at the estimate (1.5 x the width).
        layout.insert(0, {band(), page(1, 0, 960), page(1, 1, 960), page(1, 2, 960), band()});
        layout.pin({2, 0}, 0);  // page 1's top at the screen's top
        CHECK(layout.top() == doctest::Approx(240 + 960));
        CHECK(layout.pinned());
        // The pinned page and the one above it get their real heights: page 1's top stays at the screen's top.
        layout.setHeight(2, 9600);
        CHECK(layout.top() == doctest::Approx(layout.itemTop(2)));
        layout.setHeight(1, 9600);
        CHECK(layout.top() == doctest::Approx(layout.itemTop(2)));
        CHECK(layout.top() == doctest::Approx(240 + 9600));
        // A scroll lets go: from now on the middle line is the anchor.
        layout.scrollBy(100);
        CHECK_FALSE(layout.pinned());
        const StripPlace middle = layout.middle();
        layout.setHeight(3, 9600);  // below: nothing moves
        CHECK(layout.middle().item == middle.item);
        CHECK(layout.middle().share == doctest::Approx(middle.share));
    }

    TEST_CASE("StripLayout: a new view height (rotating, zooming) keeps the middle place") {
        StripLayout layout = gammaLayout();
        layout.scrollTo(layout.topFor({2, 0.3}, 360));
        layout.setViewHeight(1280);
        CHECK(layout.middle().item == 2);
        CHECK(layout.middle().share == doctest::Approx(0.3));
        // A pin at the top stays at the top.
        layout.pin({3, 0}, 0);
        layout.setViewHeight(720);
        CHECK(layout.top() == doctest::Approx(layout.itemTop(3)));
    }

    TEST_CASE("stripTileWindow: rows across page seams, the upload order, and nothing on screen") {
        // Three 800 x 12000 strips (decoded at 800 wide, portrait-like) drawn 9600 units tall: six rows of 2000 px = 1600 units.
        const std::vector<TileRect> tiles = planTiles({800, 12000}, 2048);
        REQUIRE(tiles.size() == 6);
        std::vector<StripPageTiles> pages;
        for (int i = 0; i < 3; ++i) {
            pages.push_back(StripPageTiles{1 + i, 240 + 9600.0 * i, 9600, {800, 12000}, tiles});
        }
        // The screen across the seam of pages 0 and 1: the last row of page 0 and the first of page 1 are on it.
        const double seam = 240 + 9600;
        std::vector<StripTile> kept = stripTileWindow(pages, seam - 300, seam + 420, 1);
        REQUIRE(kept.size() == 4);
        CHECK(kept[0].item == 1);
        CHECK(kept[0].tile == 5);
        CHECK(kept[0].onScreen);
        CHECK(kept[1].item == 2);
        CHECK(kept[1].tile == 0);
        CHECK(kept[1].onScreen);
        // Then one row around each way, nearest first: page 1's row 1 (1180 below) before page 0's row 4 (1300 above).
        CHECK(kept[2].item == 2);
        CHECK(kept[2].tile == 1);
        CHECK_FALSE(kept[2].onScreen);
        CHECK(kept[3].item == 1);
        CHECK(kept[3].tile == 4);
        // Four rows around reach across the seam both ways.
        kept = stripTileWindow(pages, seam - 300, seam + 420, 4);
        CHECK(kept.size() == 10);
        // Across a band: the screen on the end band below page 2 keeps the rows above it.
        const double end = 240 + 3 * 9600;
        kept = stripTileWindow(pages, end + 10, end + 730, 2);
        REQUIRE(kept.size() == 2);
        CHECK(kept[0].item == 3);
        CHECK(kept[0].tile == 5);  // the nearest first
        CHECK(kept[1].tile == 4);
        // A page not decoded yet (left out) between two decoded ones: the rows are counted across the gap.
        std::vector<StripPageTiles> gap = {pages[0], pages[2]};
        kept = stripTileWindow(gap, seam + 100, seam + 820, 1);  // the screen is on page 1 (no tiles)
        REQUIRE(kept.size() == 2);
        CHECK(kept[0].item == 1);  // page 0's last row, 100 above, before page 2's first, 8780 below
        CHECK(kept[0].tile == 5);
        CHECK(kept[1].item == 3);
        CHECK(kept[1].tile == 0);
    }

    TEST_CASE("stripPrefetchOrder: the page under the middle line, then ahead nearest first, then behind; the ends") {
        StripLayout layout;
        layout.setViewHeight(720);
        std::vector<StripItem> items = {band()};
        for (int i = 0; i < 10; ++i) {
            items.push_back(page(1, i, 1000));
        }
        items.push_back(band());
        layout.insert(0, items);
        layout.scrollTo(layout.topFor({5, 0.5}, 360));  // the middle of page 4: strip y 4740 at the middle
        // Window [top - 1 screen, bottom + 3 screens] = [3660, 7260]: pages 3..7 (items 4..8), page 4 first.
        CHECK(stripPrefetchOrder(layout, 1, 3) == std::vector<int>{5, 6, 7, 8, 4});
        CHECK(stripPrefetchOrder(layout, 0, 1) == std::vector<int>{5, 6});
        StripEnds ends = stripWindowEnds(layout, 1, 3);
        CHECK_FALSE(ends.top);
        CHECK_FALSE(ends.bottom);
        // Near the end: the window reaches the end band (the next chapter is wanted).
        layout.scrollTo(layout.topFor({10, 0.5}, 360));
        ends = stripWindowEnds(layout, 1, 3);
        CHECK(ends.bottom);
        CHECK_FALSE(ends.top);
        // At the start: the top band.
        layout.scrollTo(0);
        ends = stripWindowEnds(layout, 1, 3);
        CHECK(ends.top);
        CHECK(stripPrefetchOrder(layout, 1, 3).front() == 1);  // bands are never in the list
    }

    TEST_CASE("releaseVelocity: fast, slow, and a finger that stopped before lifting") {
        // 2000 units a second, sampled every 16 ms.
        std::vector<TouchSample> fast;
        for (int i = 0; i <= 10; ++i) {
            fast.push_back({1000 + 16 * i, 2.0 * 16 * i});
        }
        CHECK(releaseVelocity(fast, 1160) == doctest::Approx(2000).epsilon(0.01));
        // Up the screen: negative.
        std::vector<TouchSample> up;
        for (int i = 0; i <= 10; ++i) {
            up.push_back({16 * i, -0.5 * 16 * i});
        }
        CHECK(releaseVelocity(up, 160) == doctest::Approx(-500).epsilon(0.01));
        // Moved fast, then rested 80 ms before lifting: no flick.
        std::vector<TouchSample> rested = fast;
        rested.push_back({1240, fast.back().y});
        CHECK(releaseVelocity(rested, 1240) == doctest::Approx(0));
        // A finger that was down only 48 ms: the speed over what there was.
        const std::vector<TouchSample> quick = {{0, 0}, {16, 30}, {32, 60}, {48, 90}};
        CHECK(releaseVelocity(quick, 48) == doctest::Approx(1875).epsilon(0.01));
        CHECK(releaseVelocity({{0, 5}}, 10) == doctest::Approx(0));
    }

    TEST_CASE("flings: the distance, the stop, the top speed") {
        CHECK(flingStartSpeed(10, 720) == doctest::Approx(0));  // below the stop speed: no flick
        CHECK(flingStartSpeed(100000, 720) == doctest::Approx(5 * 720));
        CHECK(flingStartSpeed(-100000, 720) == doctest::Approx(-5 * 720));
        CHECK(flingStartSpeed(1500, 720) == doctest::Approx(1500));
        const double v = 2000;
        // The whole distance: v / (ln 2 / 0.25) minus the bit below the stop speed.
        const double rate = std::log(2.0) / 0.25;
        const double total = (v - kFlingStopSpeed) / rate;
        CHECK(flingOffset(v, 100) == doctest::Approx(total));
        CHECK(flingOffset(-v, 100) == doctest::Approx(-total));
        // After one half-life the speed has halved: half of the remaining distance (from v) is gone.
        CHECK(flingOffset(v, 0.25) == doctest::Approx(v / rate * 0.5));
        CHECK(flingSeconds(v) == doctest::Approx(std::log(100.0) / rate));
        CHECK_FALSE(flingDone(v, 1.0));
        CHECK(flingDone(v, 1.7));
        CHECK(flingDone(10, 0));
        // The offset only grows.
        CHECK(flingOffset(v, 0.5) > flingOffset(v, 0.25));
    }

    TEST_CASE("held D-pad and stick scrolling in the strip") {
        CHECK(stripHoldScroll(720, 0.5) == doctest::Approx(360));
        CHECK(stripHoldScroll(720, -1) == doctest::Approx(0));
        // A full push crosses a screen in 0.6 s; down scrolls down, up scrolls up; inside the dead zone nothing.
        CHECK(stripStickScroll(1.0f, 720, 0.6) == doctest::Approx(720));
        CHECK(stripStickScroll(-1.0f, 720, 0.3) == doctest::Approx(-360));
        CHECK(stripStickScroll(0.1f, 720, 1) == doctest::Approx(0));
        CHECK(std::fabs(stripStickScroll(0.5f, 720, 0.6)) < 720 * 0.3);  // a half push: much slower (the curve)
        CHECK(kStripGlideShare * 1280 == doctest::Approx(960));          // a bottom tap in portrait: 960 units
    }

    TEST_CASE("stripTapArea: top 30 % back, middle 40 % menu, bottom 30 % on") {
        CHECK(stripTapArea(0, 720) == StripTapArea::Back);
        CHECK(stripTapArea(215, 720) == StripTapArea::Back);
        CHECK(stripTapArea(217, 720) == StripTapArea::Middle);
        CHECK(stripTapArea(503, 720) == StripTapArea::Middle);
        CHECK(stripTapArea(505, 720) == StripTapArea::On);
        CHECK(stripTapArea(1279, 1280) == StripTapArea::On);
        CHECK(stripTapArea(10, 0) == StripTapArea::Middle);
    }

    TEST_CASE("the spot note: text round trip, bad text gives nothing") {
        CHECK(stripSpotValue(2, 0.4375) == "2:0.4375");
        CHECK(stripSpotValue(0, 0) == "0:0.0000");
        CHECK(stripSpotValue(5, 1) == "5:1.0000");
        CHECK(stripSpotValue(-3, 1.7) == "0:1.0000");
        CHECK(stripSpotValue(1, 0.00004) == "1:0.0000");
        CHECK(stripSpotValue(1, 0.12346) == "1:0.1235");
        for (const double share : {0.0, 0.0001, 0.25, 0.4375, 0.9999, 1.0}) {
            const auto back = parseStripSpot(stripSpotValue(7, share));
            REQUIRE(back.has_value());
            CHECK(back->page == 7);
            CHECK(back->share == doctest::Approx(share).epsilon(1e-9));
        }
        CHECK(parseStripSpot("3:1.5")->share == doctest::Approx(1));  // kept within 0..1
        CHECK(parseStripSpot("3:-0.2")->share == doctest::Approx(0));
        for (const char* bad : {"", ":", "2", "2:", ":0.5", "a:0.5", "2:x", "2:0.5x", "-1:0.5", "2:0.5:1", "2:nan", "2:inf",
                                " 2:0.5", "2:0.123456789012345678901234567890"}) {
            CAPTURE(bad);
            CHECK_FALSE(parseStripSpot(bad).has_value());
        }
    }

    TEST_CASE("by itself: the genre rule and the shape rule") {
        CHECK(looksLikeStrip({"Action", "Long Strip"}));
        CHECK(looksLikeStrip({"webtoon"}));
        CHECK(looksLikeStrip({" WEBTOONS "}));
        CHECK_FALSE(looksLikeStrip({"Manhwa", "Manhua", "Comedy"}));
        CHECK_FALSE(looksLikeStrip({"Long Strip Comedy"}));  // the whole tag
        CHECK_FALSE(looksLikeStrip({}));
        CHECK(isStripPage({800, 12000}));
        CHECK(isStripPage({800, 2001}));
        CHECK_FALSE(isStripPage({800, 2000}));
        CHECK_FALSE(isStripPage({1000, 1500}));
        CHECK_FALSE(isStripPage({0, 100}));
    }

    TEST_CASE("AppConfig: reader.stripWidth and reader.stripOrientation, old files, a round trip, own settings") {
        CHECK(stripWidthName(StripWidth::Half) == "half");
        CHECK(stripWidthName(StripWidth::ThreeQuarters) == "three-quarters");
        CHECK(stripWidthName(StripWidth::Full) == "full");
        CHECK(stripWidthFromName("full") == StripWidth::Full);
        CHECK_FALSE(stripWidthFromName("wide").has_value());
        // An old file: half, no own strip orientation (the portrait way used last), no problem reported.
        const ConfigLoad old =
            loadAppConfig(R"({"schemaVersion": 1, "reader": {"lastPortrait": "portrait-right", "splitSpreads": "never"}})");
        CHECK(old.config.reader.stripWidth == StripWidth::Half);
        CHECK_FALSE(old.config.reader.stripOrientation.has_value());
        CHECK(stripOrientationFor(old.config.reader) == ReaderOrientation::PortraitRight);
        CHECK(old.problems.empty());
        // A round trip.
        AppConfig config;
        config.reader.stripWidth = StripWidth::ThreeQuarters;
        config.reader.stripOrientation = ReaderOrientation::Landscape;
        const std::string text = saveAppConfig(config);
        CHECK(text.find("\"stripWidth\": \"three-quarters\"") != std::string::npos);
        CHECK(text.find("\"stripOrientation\": \"landscape\"") != std::string::npos);
        const ConfigLoad back = loadAppConfig(text);
        CHECK(back.config.reader.stripWidth == StripWidth::ThreeQuarters);
        CHECK(back.config.reader.stripOrientation == ReaderOrientation::Landscape);
        CHECK(stripOrientationFor(back.config.reader) == ReaderOrientation::Landscape);
        // Without its own orientation the key isn't written.
        CHECK(saveAppConfig(AppConfig{}).find("stripOrientation") == std::string::npos);
        // Unknown values keep the defaults and say so.
        const ConfigLoad odd =
            loadAppConfig(R"({"schemaVersion": 1, "reader": {"stripWidth": "wide", "stripOrientation": "upside-down"}})");
        CHECK(odd.config.reader.stripWidth == StripWidth::Half);
        CHECK_FALSE(odd.config.reader.stripOrientation.has_value());
        CHECK(odd.problems.size() == 2);
        // "Has its own direction": only once remembered; "vertical" round-trips per manga.
        ReaderSettings reader;
        CHECK_FALSE(hasOwnReaderSettings(reader, 3));
        rememberReaderSettings(reader, MangaReaderSettings{3, ReadingDirection::Vertical, PageFit::Screen}, 500);
        CHECK(hasOwnReaderSettings(reader, 3));
        CHECK_FALSE(hasOwnReaderSettings(reader, 4));
        AppConfig withManga;
        withManga.reader = reader;
        CHECK(readerSettingsFor(loadAppConfig(saveAppConfig(withManga)).config.reader, 3).direction ==
              ReadingDirection::Vertical);
    }

    TEST_CASE("ProgressSync: the strip's spot goes with the page, a moved spot is saved within the throttle") {
        // The server has page 1 at 0.25 (the note matches lastPageRead).
        ProgressSync sync(9, 1, false, 5000, 60000, 0, 0.25);
        sync.shownAt(1, 0.25, false);
        CHECK_FALSE(sync.pending());  // nothing new
        sync.shownAt(1, 0.25004, false);
        CHECK_FALSE(sync.pending());  // below the note's 4 decimals: the same spot
        sync.shownAt(1, 0.5, false);
        CHECK(sync.pending());
        CHECK_FALSE(sync.due(4999).has_value());  // the same throttle as the page
        auto progress = sync.due(5000);
        REQUIRE(progress.has_value());
        CHECK(progress->lastPageRead == 1);
        REQUIRE(progress->spot.has_value());
        CHECK(*progress->spot == doctest::Approx(0.5));
        sync.sending(*progress, 5000);
        sync.saved(*progress);
        CHECK_FALSE(sync.pending());
        // Another page: page and spot together.
        sync.shownAt(2, 0.1, false);
        progress = sync.due(10000);
        REQUIRE(progress.has_value());
        CHECK(progress->lastPageRead == 2);
        CHECK(*progress->spot == doctest::Approx(0.1));
        // Read: at once, without waiting for the interval.
        sync.sending(*progress, 10000);
        sync.saved(*progress);
        sync.shownAt(2, 0.9, true);
        progress = sync.due(10001);
        REQUIRE(progress.has_value());
        CHECK(progress->isRead);
        // The page reader's shown() never sends a spot.
        ProgressSync paged(9, 0, false, 5000, 60000, 0);
        paged.shown(3, false);
        CHECK_FALSE(paged.dueNow()->spot.has_value());
        // No note on the server yet: the first spot counts as new even on the server's page.
        ProgressSync fresh(9, 2, false, 5000, 60000, 0);
        fresh.shownAt(2, 0, false);
        CHECK(fresh.pending());
    }

    TEST_CASE("UpdateStripProgress: one request with both fields; the answer needs both") {
        ReadProgress progress;
        progress.chapterId = 31;
        progress.lastPageRead = 2;
        progress.spot = 0.4375;
        const GqlRequest request = gql::updateStripProgress(progress);
        CHECK(request.mutation);
        CHECK(request.variables ==
              nlohmann::json::parse(R"({"id": 31, "patch": {"lastPageRead": 2}, "spot": "2:0.4375"})"));
        const std::string body = request.body();
        CHECK(body.find("updateChapter(") != std::string::npos);
        CHECK(body.find("setChapterMeta(") != std::string::npos);
        CHECK(body.find("snx_strip_spot") != std::string::npos);
        progress.isRead = true;
        CHECK(gql::updateStripProgress(progress).variables["patch"] ==
              nlohmann::json::parse(R"({"lastPageRead": 2, "isRead": true})"));

        const auto both = gql::readUpdateStripProgress(answer(
            R"({"data": {"updateChapter": {"chapter": {"id": 31, "lastPageRead": 2, "isRead": false, "pageCount": 3}},
                         "setChapterMeta": {"meta": {"key": "snx_strip_spot", "value": "2:0.4375"}}}})"));
        REQUIRE(both.ok());
        CHECK(both.value().lastPageRead == 2);
        REQUIRE(both.value().spot.has_value());
        CHECK(*both.value().spot == doctest::Approx(0.4375));
        // The page saved but the note not: the save failed (it goes again).
        const auto noNote = gql::readUpdateStripProgress(answer(
            R"({"data": {"updateChapter": {"chapter": {"id": 31, "lastPageRead": 2, "isRead": false, "pageCount": 3}},
                         "setChapterMeta": null}, "errors": [{"message": "boom"}]})"));
        REQUIRE_FALSE(noNote.ok());
        CHECK(noNote.error().message == "boom");
        CHECK_FALSE(gql::readUpdateStripProgress(answer(R"({"data": {"setChapterMeta": {"meta": {"value": "1:0"}}}})")).ok());
    }

    TEST_CASE("fetchChapterPages: the chapter's spot note is read from its meta") {
        const auto pages = gql::readFetchChapterPages(answer(
            R"({"data": {"fetchChapterPages": {"pages": ["/a", "/b"], "chapter": {"id": 31, "lastPageRead": 1,
                "meta": [{"key": "other", "value": "x"}, {"key": "snx_strip_spot", "value": "1:0.5000"}]}}}})"));
        REQUIRE(pages.ok());
        CHECK(pages.value().chapter.stripSpotNote == "1:0.5000");
        const auto none = gql::readFetchChapterPages(
            answer(R"({"data": {"fetchChapterPages": {"pages": [], "chapter": {"id": 31, "meta": [5, {"key": 1}]}}}})"));
        REQUIRE(none.ok());
        CHECK(none.value().chapter.stripSpotNote.empty());
    }
}
