// core's zoom and pan rules (WP-13): zoom steps and limits, zooming around a point, the pinch, the double tap, what the
// D-pad and the sticks do zoomed and not zoomed (in all three orientations), the fresh-press rule, the zoomed decode box,
// the "sharp" rule, the zoom-on-turn setting and the profile's maxZoom.

#include <cmath>
#include <string>

#include <doctest/doctest.h>

#include "core/AppConfig.hpp"
#include "core/ReaderMath.hpp"
#include "core/RuntimeProfile.hpp"

using namespace snx::core;

namespace {

bool near(PointF a, PointF b, float slack = 0.01f) {
    return std::fabs(a.x - b.x) <= slack && std::fabs(a.y - b.y) <= slack;
}

// The page point (in shares of the page) under screen point `at`.
PointF pagePointUnder(PointF at, PointF position, SizeF fit, float zoom) {
    return {(at.x - position.x) / (fit.width * zoom), (at.y - position.y) / (fit.height * zoom)};
}

// No gap at an edge: a page bigger than the screen covers it; a smaller one is centred.
bool noGap(PointF position, SizeF page, SizeF screen) {
    const auto axis = [](float corner, float size, float view) {
        if (size <= view) {
            return std::fabs(corner - (view - size) / 2) < 0.01f;
        }
        return corner <= 0.01f && corner + size >= view - 0.01f;
    };
    return axis(position.x, page.width, screen.width) && axis(position.y, page.height, screen.height);
}

const SizeF kLandscape{1280, 720};
const SizeF kPortrait{720, 1280};
const SizeF kFit{480, 720};  // a 1000 x 1500 page fitted to the landscape screen

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("Zoom: steps 1, 1.5, 2, 3, 4, capped at maxZoom") {
        CHECK(zoomStep(1, 1, 4) == doctest::Approx(1.5));
        CHECK(zoomStep(1.5f, 1, 4) == doctest::Approx(2));
        CHECK(zoomStep(2, 1, 4) == doctest::Approx(3));
        CHECK(zoomStep(3, 1, 4) == doctest::Approx(4));
        CHECK(zoomStep(4, 1, 4) == doctest::Approx(4));      // at the top: stays
        CHECK(zoomStep(4, -1, 4) == doctest::Approx(3));
        CHECK(zoomStep(1.5f, -1, 4) == doctest::Approx(1));
        CHECK(zoomStep(1, -1, 4) == doctest::Approx(1));     // at the fit: stays
        CHECK(zoomStep(1.7f, 1, 4) == doctest::Approx(2));   // from between two steps (a pinch): the next one
        CHECK(zoomStep(1.7f, -1, 4) == doctest::Approx(1.5));
        CHECK(zoomStep(2, 1, 2) == doctest::Approx(2));      // safe mode's cap
        CHECK(zoomStep(1.5f, 1, 2) == doctest::Approx(2));
        CHECK(zoomStep(2, 1, 2.5f) == doctest::Approx(2.5)); // a cap between steps is a step
        CHECK(zoomStep(2.5f, -1, 2.5f) == doctest::Approx(2));
        CHECK(clampZoom(0.5f, 4) == doctest::Approx(1));
        CHECK(clampZoom(9, 4) == doctest::Approx(4));
        CHECK(clampZoom(2.2f, 4) == doctest::Approx(2.2));
        CHECK(clampZoom(3, 0.5f) == doctest::Approx(1));     // a broken cap never zooms out below the fit
    }

    TEST_CASE("Zoom: the decode buckets, the snap back to the fit, zoomed or not") {
        CHECK(zoomBucket(1, 4) == doctest::Approx(1));
        CHECK(zoomBucket(1.2f, 4) == doctest::Approx(1.5));
        CHECK(zoomBucket(1.5f, 4) == doctest::Approx(1.5));
        CHECK(zoomBucket(1.7f, 4) == doctest::Approx(2));
        CHECK(zoomBucket(2.01f, 4) == doctest::Approx(3));
        CHECK(zoomBucket(3.9f, 4) == doctest::Approx(4));
        CHECK(zoomBucket(3.9f, 2) == doctest::Approx(2));    // never above the cap
        CHECK(zoomBucket(0.8f, 4) == doctest::Approx(1));
        CHECK(snapsToFit(1.04f));
        CHECK_FALSE(snapsToFit(1.06f));
        CHECK_FALSE(isZoomed(1));
        CHECK_FALSE(isZoomed(1.0005f));
        CHECK(isZoomed(1.01f));
    }

    TEST_CASE("Zoom: zoomAround keeps the point under the anchor and never shows a gap") {
        // From the fit (centred: corner 400, 0) to 2 x around the middle of the screen.
        const PointF fitCorner = clampPosition({0, 0}, kFit, kLandscape);
        CHECK(near(fitCorner, {400, 0}));
        const PointF anchor{640, 360};
        const PointF zoomed = zoomAround(fitCorner, kFit, 1, 2, anchor, kLandscape);
        CHECK(near(pagePointUnder(anchor, zoomed, kFit, 2), pagePointUnder(anchor, fitCorner, kFit, 1)));
        CHECK(noGap(zoomed, {960, 1440}, kLandscape));
        // 960 wide is still narrower than 1280: centred sideways; the point stays put up and down.
        CHECK(zoomed.x == doctest::Approx(160));
        CHECK(zoomed.y == doctest::Approx(-360));
        // Anchors all over the screen, zooms in and out: the point stays (unless an edge stops it) and no gap shows.
        for (const PointF at : {PointF{100, 100}, PointF{640, 360}, PointF{1200, 700}, PointF{500, 20}}) {
            for (const float to : {1.5f, 2.0f, 3.0f, 4.0f}) {
                CAPTURE(at.x);
                CAPTURE(to);
                const PointF in = zoomAround(fitCorner, kFit, 1, to, at, kLandscape);
                const SizeF page{kFit.width * to, kFit.height * to};
                CHECK(noGap(in, page, kLandscape));
                const PointF back = zoomAround(in, kFit, to, 1, at, kLandscape);
                CHECK(near(back, fitCorner));  // back at the fit: centred again
            }
        }
        // At 4 x (1920 x 2880) around a point well inside the page, the point stays exactly.
        const PointF deep = zoomAround(zoomAround(fitCorner, kFit, 1, 2, anchor, kLandscape), kFit, 2, 4, anchor, kLandscape);
        CHECK(near(pagePointUnder(anchor, deep, kFit, 4), pagePointUnder(anchor, fitCorner, kFit, 1)));
        // An empty fit doesn't divide by zero.
        CHECK(near(zoomAround({0, 0}, {0, 0}, 1, 2, anchor, kLandscape), {640, 360}));
    }

    TEST_CASE("Zoom: the pinch follows the fingers' distance and their midpoint") {
        PinchStart start;
        start.zoom = 1;
        start.position = {400, 0};
        start.a = {540, 360};
        start.b = {740, 360};  // 200 apart, around the middle
        // Twice as far apart: 2 x, the point between them stays put.
        ZoomView view = pinchView(start, {440, 360}, {840, 360}, kFit, kLandscape, 4);
        CHECK(view.zoom == doctest::Approx(2));
        CHECK(near(pagePointUnder({640, 360}, view.position, kFit, 2), pagePointUnder({640, 360}, start.position, kFit, 1)));
        // Both fingers move 100 down while apart: the page follows them (pans).
        ZoomView moved = pinchView(start, {440, 260}, {840, 260}, kFit, kLandscape, 4);
        CHECK(moved.zoom == doctest::Approx(2));
        CHECK(moved.position.y == doctest::Approx(view.position.y - 100));
        // Three times: 3 x. Much further: the cap.
        CHECK(pinchView(start, {340, 360}, {940, 360}, kFit, kLandscape, 4).zoom == doctest::Approx(3));
        CHECK(pinchView(start, {0, 360}, {1280, 360}, kFit, kLandscape, 4).zoom == doctest::Approx(4));
        // Closer together than at the start: not below the fit.
        CHECK(pinchView(start, {600, 360}, {680, 360}, kFit, kLandscape, 4).zoom == doctest::Approx(1));
        // From a zoom of 2: half the distance goes back to 1, and that lets go as the fit.
        start.zoom = 2;
        start.position = view.position;
        const ZoomView out = pinchView(start, {590, 360}, {690, 360}, kFit, kLandscape, 4);
        CHECK(out.zoom == doctest::Approx(1));
        CHECK(near(out.position, {400, 0}));
        CHECK(snapsToFit(pinchView(start, {588, 360}, {692, 360}, kFit, kLandscape, 4).zoom));
        // Fingers that start at one point: the zoom stays.
        start.a = start.b = {640, 360};
        CHECK(pinchView(start, {500, 360}, {800, 360}, kFit, kLandscape, 4).zoom == doctest::Approx(2));
        // Never a gap, wherever the fingers go.
        start.a = {100, 100};
        start.b = {300, 100};
        const ZoomView far = pinchView(start, {1100, 600}, {1300, 700}, kFit, kLandscape, 4);
        CHECK(noGap(far.position, {kFit.width * far.zoom, kFit.height * far.zoom}, kLandscape));
    }

    TEST_CASE("Zoom: a double tap is quick, close and in the middle") {
        const float width = 1280;
        CHECK(isDoubleTap({640, 360}, 1000, {650, 370}, 1200, width));
        CHECK(isDoubleTap({640, 360}, 1000, {640, 360}, 1250, width));       // 250 ms: still in time
        CHECK_FALSE(isDoubleTap({640, 360}, 1000, {640, 360}, 1251, width)); // too late
        CHECK_FALSE(isDoubleTap({640, 360}, 1000, {640, 360}, 999, width));  // before the first lifted (not a sequence)
        CHECK_FALSE(isDoubleTap({640, 360}, 1000, {700, 360}, 1100, width)); // 60 units away
        CHECK(isDoubleTap({640, 360}, 1000, {668, 388}, 1100, width));       // ~39.6 units: close enough
        CHECK_FALSE(isDoubleTap({100, 360}, 1000, {100, 360}, 1100, width)); // the left edge turns pages
        CHECK_FALSE(isDoubleTap({1200, 360}, 1000, {1200, 360}, 1100, width));
        CHECK_FALSE(isDoubleTap({400, 360}, 1000, {370, 360}, 1100, width)); // the second one is in the left zone (< 384)
        CHECK(isDoubleTap({360, 400}, 0, {360, 410}, 100, 720));             // portrait: the reader screen's width
    }

    TEST_CASE("Zoom: what a D-pad press does, zoomed or not, and the fresh-press rule") {
        // Not zoomed: left/right turn on a fresh press only; up/down move (a tall page), held or not.
        CHECK(arrowEffect(Arrow::Left, false, false, true) == ArrowEffect::Turn);
        CHECK(arrowEffect(Arrow::Right, false, true, true) == ArrowEffect::Turn);  // fit height: still turns, as before
        CHECK(arrowEffect(Arrow::Right, false, false, false) == ArrowEffect::Nothing);  // held: one press, one page
        CHECK(arrowEffect(Arrow::Up, false, false, true) == ArrowEffect::Move);
        CHECK(arrowEffect(Arrow::Down, false, true, false) == ArrowEffect::Move);
        // Zoomed: move while there is more of the page that way, held or not.
        CHECK(arrowEffect(Arrow::Left, true, true, true) == ArrowEffect::Move);
        CHECK(arrowEffect(Arrow::Left, true, true, false) == ArrowEffect::Move);
        CHECK(arrowEffect(Arrow::Up, true, true, false) == ArrowEffect::Move);
        // At the edge: a fresh press toward a side turns; held, or up/down, nothing.
        CHECK(arrowEffect(Arrow::Right, true, false, true) == ArrowEffect::Turn);
        CHECK(arrowEffect(Arrow::Right, true, false, false) == ArrowEffect::Nothing);
        CHECK(arrowEffect(Arrow::Down, true, false, true) == ArrowEffect::Nothing);
        CHECK(arrowEffect(Arrow::Up, true, false, true) == ArrowEffect::Nothing);
        CHECK(arrowSide(Arrow::Left) == Side::Left);
        CHECK(arrowSide(Arrow::Right) == Side::Right);
        CHECK_FALSE(arrowSide(Arrow::Up).has_value());
        // One press moves a zoomed page three quarters of the screen, against the arrow (right shows more on the right).
        CHECK(near(arrowMove(Arrow::Right, kLandscape), {-960, 0}));
        CHECK(near(arrowMove(Arrow::Left, kLandscape), {960, 0}));
        CHECK(near(arrowMove(Arrow::Down, kPortrait), {0, -960}));
        CHECK(near(arrowMove(Arrow::Up, kPortrait), {0, 960}));
        CHECK(glideProgress(0) == doctest::Approx(0));
        CHECK(glideProgress(1) == doctest::Approx(1));
        CHECK(glideProgress(2) == doctest::Approx(1));
        CHECK(glideProgress(0.5f) > 0.5f);  // eases out: fast first
    }

    TEST_CASE("Zoom: where the page can still move, all four ways") {
        const SizeF page{1920, 2880};  // 4 x the fit
        CHECK(canMoveToward(PointF{-100, -100}, page, kLandscape, Arrow::Left));
        CHECK(canMoveToward(PointF{-100, -100}, page, kLandscape, Arrow::Up));
        CHECK(canMoveToward(PointF{-100, -100}, page, kLandscape, Arrow::Right));
        CHECK(canMoveToward(PointF{-100, -100}, page, kLandscape, Arrow::Down));
        CHECK_FALSE(canMoveToward(PointF{0, 0}, page, kLandscape, Arrow::Left));     // at the left and top edges
        CHECK_FALSE(canMoveToward(PointF{0, 0}, page, kLandscape, Arrow::Up));
        CHECK_FALSE(canMoveToward(PointF{-640, -2160}, page, kLandscape, Arrow::Right));  // at the right and bottom edges
        CHECK_FALSE(canMoveToward(PointF{-640, -2160}, page, kLandscape, Arrow::Down));
        CHECK_FALSE(canMoveToward(PointF{400, 0}, kFit, kLandscape, Arrow::Down));   // the fit: nowhere
    }

    TEST_CASE("Zoom: the sticks, turned with the screen, with a dead zone and a speed curve") {
        // Landscape: as the console is held. Turned left, a push to the right points up the reader's screen; turned right, down.
        CHECK(near(readerStick(ReaderOrientation::Landscape, {1, 0}), {1, 0}));
        CHECK(near(readerStick(ReaderOrientation::PortraitLeft, {1, 0}), {0, -1}));
        CHECK(near(readerStick(ReaderOrientation::PortraitRight, {1, 0}), {0, 1}));
        CHECK(near(readerStick(ReaderOrientation::PortraitLeft, {0, -1}), {-1, 0}));  // up = the reader's left
        CHECK(near(readerStick(ReaderOrientation::PortraitRight, {0, 1}), {-1, 0}));   // down = the reader's left
        // The same as the D-pad turns.
        CHECK(stickArrow(readerStick(ReaderOrientation::PortraitLeft, {0, -1})) ==
              readerArrow(ReaderOrientation::PortraitLeft, Arrow::Up));
        CHECK(stickArrow(readerStick(ReaderOrientation::PortraitRight, {0, 1})) ==
              readerArrow(ReaderOrientation::PortraitRight, Arrow::Down));
        // Dead zone and curve.
        CHECK(stickSpeed(0.1f) == doctest::Approx(0));
        CHECK(stickSpeed(0.15f) == doctest::Approx(0));
        CHECK(stickSpeed(1) == doctest::Approx(1));
        CHECK(stickSpeed(-1) == doctest::Approx(1));
        CHECK(stickSpeed(0.575f) == doctest::Approx(0.25));  // half way past the dead zone: a quarter of the speed
        CHECK(stickSpeed(0.5f) < stickSpeed(0.8f));
        // A full push crosses one screen in 0.6 s; the page moves against the push.
        CHECK(near(stickMove({1, 0}, kLandscape, 0.6f), {-1280, 0}, 0.5f));
        CHECK(near(stickMove({0, -1}, kPortrait, 0.3f), {0, 640}, 0.5f));
        CHECK(near(stickMove({0.1f, 0.1f}, kLandscape, 1), {0, 0}));  // inside the (round) dead zone
        // Half way counts as a D-pad press; the larger direction wins.
        CHECK_FALSE(stickArrow({0.45f, 0.3f}).has_value());
        CHECK(stickArrow({0.6f, 0.3f}) == Arrow::Right);
        CHECK(stickArrow({-0.6f, 0.7f}) == Arrow::Down);
        CHECK(stickArrow({-0.9f, 0}) == Arrow::Left);
        CHECK(stickArrow({0, -0.5f}) == Arrow::Up);
    }

    TEST_CASE("Zoom: the decode box for the page on screen, per fit and orientation") {
        const ImageSize landscape{1280, 720};
        const ImageSize portrait{720, 1280};
        // Zoom 1 = the fit's own box.
        CHECK(zoomDecodeBox(PageFit::Screen, landscape, 1, 4, 2) == ImageSize{1280, 720});
        CHECK(zoomDecodeBox(PageFit::Width, landscape, 1, 4, 2) == ImageSize{1280, 0});
        CHECK(zoomDecodeBox(PageFit::Height, portrait, 1, 4, 2) == ImageSize{0, 1280});
        // The bucket, not the exact zoom: 1.2 decodes for 1.5, 1.7 for 2.
        CHECK(zoomDecodeBox(PageFit::Screen, landscape, 1.2f, 4, 2) == ImageSize{1920, 1080});
        CHECK(zoomDecodeBox(PageFit::Screen, landscape, 1.7f, 4, 2) == ImageSize{2560, 1440});
        // Capped at maxDecodeScale x the reader's screen as it is held: portrait 2 x 720 wide, 2 x 1280 tall.
        CHECK(zoomDecodeBox(PageFit::Screen, landscape, 4, 4, 2) == ImageSize{2560, 1440});
        CHECK(zoomDecodeBox(PageFit::Screen, portrait, 2, 4, 2) == ImageSize{1440, 2560});
        CHECK(zoomDecodeBox(PageFit::Screen, portrait, 4, 4, 2) == ImageSize{1440, 2560});
        CHECK(zoomDecodeBox(PageFit::Screen, portrait, 4, 4, 4) == ImageSize{2880, 5120});
        CHECK(zoomDecodeBox(PageFit::Width, portrait, 3, 4, 2) == ImageSize{1440, 0});
        CHECK(zoomDecodeBox(PageFit::Height, landscape, 2, 4, 2) == ImageSize{0, 1440});
        // maxDecodeScale 1 (the applet profile): no zoomed decode at all, the fit's box.
        CHECK(zoomDecodeBox(PageFit::Screen, landscape, 2, 2, 1) == ImageSize{1280, 720});
        // A 1000 x 1500 page (Alpha) in portrait at 2 x: the box 1440 x 2560 holds the whole original: sharp at 1000 x 1500.
        const ImageSize alpha{1000, 1500};
        const ImageSize box = zoomDecodeBox(PageFit::Screen, portrait, 2, 4, 2);
        CHECK(fitInside(alpha, box) == alpha);
        CHECK(isSharp({1000, 1500}, alpha, box));
        CHECK_FALSE(isSharp({720, 1080}, alpha, box));  // the fit decode, stretched: blurry
        // The fit decode is sharp at the fit; a decode up to 15 % over the box (the shrink slack) too.
        CHECK(isSharp({720, 1080}, alpha, portrait));
        CHECK(isSharp({500, 750}, alpha, landscape));  // JPEG at 1/2 for the 480 x 720 fit
        // A source with less detail than the zoom wants is as sharp as it gets.
        CHECK(isSharp({400, 600}, {400, 600}, {2560, 1440}));
        // A decode made smaller by the memory budget isn't sharp.
        CHECK_FALSE(isSharp({800, 1200}, alpha, box));
    }

    TEST_CASE("AppConfig: reader.zoomOnTurn, old files without it, a round trip") {
        CHECK(zoomOnTurnName(ZoomOnTurn::Reset) == "reset");
        CHECK(zoomOnTurnName(ZoomOnTurn::Keep) == "keep");
        CHECK(zoomOnTurnFromName("keep") == ZoomOnTurn::Keep);
        CHECK_FALSE(zoomOnTurnFromName("sometimes").has_value());
        // An old file: the default (back to the whole page), no problem reported.
        const ConfigLoad old = loadAppConfig(R"({"schemaVersion": 1, "reader": {"direction": "ltr"}})");
        CHECK(old.config.reader.zoomOnTurn == ZoomOnTurn::Reset);
        CHECK(old.problems.empty());
        // A round trip keeps it.
        AppConfig config;
        config.reader.zoomOnTurn = ZoomOnTurn::Keep;
        const std::string text = saveAppConfig(config);
        CHECK(text.find("\"zoomOnTurn\": \"keep\"") != std::string::npos);
        CHECK(loadAppConfig(text).config.reader.zoomOnTurn == ZoomOnTurn::Keep);
        // An unknown value keeps the default and says so.
        const ConfigLoad odd = loadAppConfig(R"({"schemaVersion": 1, "reader": {"zoomOnTurn": 3}})");
        CHECK(odd.config.reader.zoomOnTurn == ZoomOnTurn::Reset);
        CHECK(odd.problems.size() == 1);
    }

    TEST_CASE("RuntimeProfile: maxZoom 4 in the full profile, 2 in safe mode and in the applet profile") {
        const RuntimeProfile full = makeRuntimeProfile(LaunchMode::Application, 4 * kGiB, 200 * kMiB);
        CHECK(full.maxZoom == doctest::Approx(4));
        CHECK(makeRuntimeProfile(LaunchMode::Application, 4 * kGiB, 200 * kMiB, true).maxZoom == doctest::Approx(2));
        CHECK(makeRuntimeProfile(LaunchMode::Applet, 512 * kMiB, 100 * kMiB).maxZoom == doctest::Approx(2));
    }
}
