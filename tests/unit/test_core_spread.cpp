// core's split-spread rules (WP-13c): when a spread shows as two halves (every mode x orientation), which half comes first
// in each reading direction, the halves' rectangles (odd widths too), the doubled decode box and its zoomed cap, and the
// "Split spreads" setting in config.json.

#include <string>

#include <doctest/doctest.h>

#include "core/AppConfig.hpp"
#include "core/ImageGeometry.hpp"
#include "core/ReaderMath.hpp"

using namespace snx::core;

TEST_SUITE("core") {

    TEST_CASE("splitsSpread: in portrait only by default, always, never; never a page that isn't a spread") {
        const ReaderOrientation all[] = {ReaderOrientation::Landscape, ReaderOrientation::PortraitLeft,
                                         ReaderOrientation::PortraitRight};
        for (const ReaderOrientation orientation : all) {
            const std::string name(readerOrientationName(orientation));
            CAPTURE(name);
            const bool portrait = orientation != ReaderOrientation::Landscape;
            CHECK(splitsSpread(SpreadSplit::Portrait, orientation, true) == portrait);
            CHECK(splitsSpread(SpreadSplit::Always, orientation, true));
            CHECK_FALSE(splitsSpread(SpreadSplit::Never, orientation, true));
            // A regular page is never split.
            CHECK_FALSE(splitsSpread(SpreadSplit::Portrait, orientation, false));
            CHECK_FALSE(splitsSpread(SpreadSplit::Always, orientation, false));
            CHECK_FALSE(splitsSpread(SpreadSplit::Never, orientation, false));
        }
        // What isSpread counts: Beta's 2000 x 1500 spread, not a 1000 x 1500 page or a square one.
        CHECK(isSpread({2000, 1500}));
        CHECK_FALSE(isSpread({1000, 1500}));
        CHECK_FALSE(isSpread({1200, 1000}));  // exactly 1.2: not a spread
    }

    TEST_CASE("spreadHalfSide: right to left starts with the right half, left to right and vertical with the left one") {
        CHECK(spreadHalfSide(0, ReadingDirection::RightToLeft) == Side::Right);
        CHECK(spreadHalfSide(1, ReadingDirection::RightToLeft) == Side::Left);
        CHECK(spreadHalfSide(0, ReadingDirection::LeftToRight) == Side::Left);
        CHECK(spreadHalfSide(1, ReadingDirection::LeftToRight) == Side::Right);
        CHECK(spreadHalfSide(0, ReadingDirection::Vertical) == Side::Left);
        CHECK(spreadHalfSide(1, ReadingDirection::Vertical) == Side::Right);
    }

    TEST_CASE("spreadHalf: the two halves cover the image exactly, odd widths included") {
        const TileRect left = spreadHalf({2000, 1500}, Side::Left);
        const TileRect right = spreadHalf({2000, 1500}, Side::Right);
        CHECK(left.x == 0);
        CHECK(left.y == 0);
        CHECK(left.width == 1000);
        CHECK(left.height == 1500);
        CHECK(right.x == 1000);
        CHECK(right.width == 1000);
        CHECK(right.height == 1500);
        // Odd: the left half is the narrower one, and they meet without a gap or an overlap.
        for (const int width : {1, 3, 1441, 2001}) {
            CAPTURE(width);
            const TileRect l = spreadHalf({width, 1080}, Side::Left);
            const TileRect r = spreadHalf({width, 1080}, Side::Right);
            CHECK(l.x == 0);
            CHECK(l.width + r.width == width);
            CHECK(r.x == l.width);
            CHECK(r.width - l.width == 1);
        }
        CHECK(spreadHalfSize({1441, 1080}, Side::Left) == ImageSize{720, 1080});
        CHECK(spreadHalfSize({1441, 1080}, Side::Right) == ImageSize{721, 1080});
        // Nothing to halve.
        CHECK(spreadHalfSize({0, 1080}, Side::Left) == ImageSize{0, 0});
    }

    TEST_CASE("a half fits the portrait screen like a page: Beta's spread half is 720 x 1080 there, 480 x 720 in landscape") {
        const ImageSize half = spreadHalfSize({2000, 1500}, Side::Right);
        const SizeF portrait = fitPage(half, {720, 1280}, PageFit::Screen);
        CHECK(portrait.width == doctest::Approx(720));
        CHECK(portrait.height == doctest::Approx(1080));
        // In landscape a half is no bigger than the whole spread (960 x 720): why "In portrait" is the default.
        const SizeF landscape = fitPage(half, {1280, 720}, PageFit::Screen);
        CHECK(landscape.height == doctest::Approx(720));
        CHECK(landscape.width == doctest::Approx(480));
        CHECK(fitPage({2000, 1500}, {1280, 720}, PageFit::Screen).height == doctest::Approx(720));
    }

    TEST_CASE("splitDecodeBox: twice as wide, so each half gets a page's pixels; no width limit stays none") {
        CHECK(splitDecodeBox({720, 1280}) == ImageSize{1440, 1280});
        CHECK(splitDecodeBox({720, 0}) == ImageSize{1440, 0});     // fit width
        CHECK(splitDecodeBox({0, 1280}) == ImageSize{0, 1280});    // fit height: no width limit anyway
        CHECK(splitDecodeBox({0, 0}) == ImageSize{0, 0});
        // Beta's spread in portrait: the doubled box decodes it 1440 wide, so the half drawn 720 wide has 720 pixels (the
        // plain box would give 720 wide: 360 for the half).
        CHECK(fitInside({2000, 1500}, splitDecodeBox({720, 1280})) == ImageSize{1440, 1080});
        CHECK(fitInside({2000, 1500}, ImageSize{720, 1280}) == ImageSize{720, 540});
        CHECK(isSharp({1440, 1080}, {2000, 1500}, splitDecodeBox({720, 1280})));
        CHECK_FALSE(isSharp({720, 540}, {2000, 1500}, splitDecodeBox({720, 1280})));
    }

    TEST_CASE("zoomDecodeBox for a split spread: the doubled box at zoom 1, capped at maxDecodeScale x the reader screen") {
        const ImageSize portrait{720, 1280};
        // Zoom 1: the doubled fit box, whatever the cap.
        CHECK(zoomDecodeBox(PageFit::Screen, portrait, 1, 4, 2, true) == ImageSize{1440, 1280});
        CHECK(zoomDecodeBox(PageFit::Screen, portrait, 1, 4, 1, true) == ImageSize{1440, 1280});  // never below the fit's
        CHECK(zoomDecodeBox(PageFit::Screen, portrait, 1, 4, 2, false) == ImageSize{720, 1280});
        // Zoom 4 at maxDecodeScale 2: 2 x 720 = 1440 wide (no sharper than zoom 1 for Beta's spread), 2 x 1280 tall.
        const ImageSize at2 = zoomDecodeBox(PageFit::Screen, portrait, 4, 4, 2, true);
        CHECK(at2 == ImageSize{1440, 2560});
        CHECK(fitInside({2000, 1500}, at2) == ImageSize{1440, 1080});
        // At maxDecodeScale 4: 2880 wide, so the whole 2000 x 1500 original.
        const ImageSize at4 = zoomDecodeBox(PageFit::Screen, portrait, 4, 4, 4, true);
        CHECK(at4 == ImageSize{2880, 5120});
        CHECK(fitInside({2000, 1500}, at4) == ImageSize{2000, 1500});
        // Zoom 2 at 4: the doubled box x 2.
        CHECK(zoomDecodeBox(PageFit::Screen, portrait, 2, 4, 4, true) == ImageSize{2880, 2560});
        // Landscape ("Always"): 2560 x 720 at zoom 1.
        CHECK(zoomDecodeBox(PageFit::Screen, {1280, 720}, 1, 4, 2, true) == ImageSize{2560, 720});
        // Fit width in portrait: no height limit.
        CHECK(zoomDecodeBox(PageFit::Width, portrait, 1, 4, 2, true) == ImageSize{1440, 0});
    }

    TEST_CASE("AppConfig: reader.splitSpreads, old files without it, a round trip") {
        CHECK(spreadSplitName(SpreadSplit::Portrait) == "portrait");
        CHECK(spreadSplitName(SpreadSplit::Always) == "always");
        CHECK(spreadSplitName(SpreadSplit::Never) == "never");
        CHECK(spreadSplitFromName("always") == SpreadSplit::Always);
        CHECK_FALSE(spreadSplitFromName("landscape").has_value());
        // An old file: the default (in portrait), no problem reported.
        const ConfigLoad old = loadAppConfig(R"({"schemaVersion": 1, "reader": {"zoomOnTurn": "keep"}})");
        CHECK(old.config.reader.splitSpreads == SpreadSplit::Portrait);
        CHECK(old.problems.empty());
        // A round trip keeps each value.
        for (const SpreadSplit mode : {SpreadSplit::Portrait, SpreadSplit::Always, SpreadSplit::Never}) {
            AppConfig config;
            config.reader.splitSpreads = mode;
            const std::string text = saveAppConfig(config);
            CHECK(text.find("\"splitSpreads\": \"" + std::string(spreadSplitName(mode)) + "\"") != std::string::npos);
            CHECK(loadAppConfig(text).config.reader.splitSpreads == mode);
        }
        // An unknown value keeps the default and says so.
        const ConfigLoad odd = loadAppConfig(R"({"schemaVersion": 1, "reader": {"splitSpreads": "sometimes"}})");
        CHECK(odd.config.reader.splitSpreads == SpreadSplit::Portrait);
        CHECK(odd.problems.size() == 1);
    }
}
