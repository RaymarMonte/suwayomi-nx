#include "ui/reader/ReaderActivity.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "app/ConfigStore.hpp"
#include "app/TestHooks.hpp"
#include "platform/Platform.hpp"
#include "ui/components/StatusView.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr float kMessageWidth = 800;
constexpr float kBarHeight = 72;         // D16: touch targets of at least 72 px
constexpr float kBottomBarHeight = 96;
constexpr float kButtonWidth = 200;
constexpr float kPortraitButtonWidth = 168;  // WP-22: four bar items fit across 720
constexpr float kBarPadding = 32;
constexpr float kPortraitBarPadding = 16;
constexpr float kMessageMargin = 40;
constexpr float kScrollShare = 0.5f;     // D-pad up/down moves a tall page by half a screen
// WP-13: a zoom that hasn't changed for this long decodes the page again for it; a held D-pad button repeats after the
// first wait, then every repeat (like borealis's own repeat); a frame longer than kMaxStickStepMs moves the stick's pan no
// further than that (a stalled frame doesn't jump the page).
constexpr std::int64_t kZoomSettleMs = 150;
constexpr std::int64_t kRepeatFirstMs = 250;
constexpr std::int64_t kRepeatMs = 100;
constexpr std::int64_t kMaxStickStepMs = 50;
constexpr float kStickDeadZone = 0.15f;
const core::Arrow kArrows[] = {core::Arrow::Up, core::Arrow::Down, core::Arrow::Left, core::Arrow::Right};
const brls::ControllerButton kDpad[] = {brls::BUTTON_UP, brls::BUTTON_DOWN, brls::BUTTON_LEFT, brls::BUTTON_RIGHT};
const core::ZoomOnTurn kZoomOnTurns[] = {core::ZoomOnTurn::Reset, core::ZoomOnTurn::Keep};
const char* const kZoomOnTurnKeys[] = {"reader/zoom_on_turn_reset", "reader/zoom_on_turn_keep"};
const NVGcolor kBarColor = nvgRGBA(16, 16, 16, 225);
const NVGcolor kBarText = nvgRGB(235, 235, 235);
const NVGcolor kPageNumberBackground = nvgRGBA(16, 16, 16, 180);

const core::ReadingDirection kDirections[] = {core::ReadingDirection::LeftToRight, core::ReadingDirection::RightToLeft};
const char* const kDirectionKeys[] = {"reader/direction_ltr", "reader/direction_rtl"};
const core::PageFit kFits[] = {core::PageFit::Screen, core::PageFit::Width, core::PageFit::Height};
const char* const kFitKeys[] = {"reader/fit_screen", "reader/fit_width", "reader/fit_height"};
const core::ReaderBackground kBackgrounds[] = {core::ReaderBackground::Black, core::ReaderBackground::Gray,
                                               core::ReaderBackground::White};
const char* const kBackgroundKeys[] = {"reader/background_black", "reader/background_gray", "reader/background_white"};
const core::ReaderOrientation kOrientations[] = {core::ReaderOrientation::Landscape, core::ReaderOrientation::PortraitLeft,
                                                 core::ReaderOrientation::PortraitRight};
const char* const kOrientationKeys[] = {"reader/orientation_landscape", "reader/orientation_portrait_left",
                                        "reader/orientation_portrait_right"};

template <typename T, std::size_t N>
int indexOf(const T (&values)[N], T value) {
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == value) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

NVGcolor backgroundColor(core::ReaderBackground background) {
    switch (background) {
        case core::ReaderBackground::Gray:
            return nvgRGB(72, 72, 72);
        case core::ReaderBackground::White:
            return nvgRGB(255, 255, 255);
        case core::ReaderBackground::Black:
            break;
    }
    return nvgRGB(0, 0, 0);
}

NVGcolor textColor(core::ReaderBackground background) {
    return background == core::ReaderBackground::White ? nvgRGB(30, 30, 30) : nvgRGB(235, 235, 235);
}

std::string imageErrorText(const image::ImageError& error) {
    switch (error.kind) {
        case image::ImageErrorKind::Network:
        case image::ImageErrorKind::Timeout:
            return platform::networkConnected() ? "reader/page_error_network"_i18n : "status/no_network"_i18n;
        case image::ImageErrorKind::Http:
            return brls::getStr("reader/page_error_http", error.httpStatus);
        case image::ImageErrorKind::TooLarge:
            return "reader/page_error_too_large"_i18n;
        case image::ImageErrorKind::Unsupported:
            return "reader/page_error_unsupported"_i18n;
        case image::ImageErrorKind::Corrupt:
            return "reader/page_error_corrupt"_i18n;
        case image::ImageErrorKind::Busy:
        case image::ImageErrorKind::Paused:
        case image::ImageErrorKind::Cancelled:
            break;
    }
    return "reader/page_error_busy"_i18n;
}

std::string chapterName(const core::Chapter& chapter) {
    return chapter.name.empty() ? brls::getStr("reader/chapter_number", chapter.sourceOrder) : chapter.name;
}

ReaderOptions optionsFor(app::AppContext& context) {
    ReaderOptions options = readerOptionsFrom(context.profile, context.config.config().reader);
    const int width = static_cast<int>(brls::Application::windowWidth);
    const int height = static_cast<int>(brls::Application::windowHeight);
    if (width > 0 && height > 0) {
        options.screen = {width, height};  // real pixels: 1280 x 720 handheld, 1920 x 1080 docked
    }
    options.clock = app::appClockMs;
    // No `this`: it also runs for the save that goes out after the reader closed.
    options.onSaved = [](const core::ReadProgress& progress, bool ok) {
        if (!ok) {
            brls::Logger::warning("reader: progress of chapter {} (page {}) wasn't saved", progress.chapterId,
                                  progress.lastPageRead);
        }
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["chapterId"] = progress.chapterId;
        fields["page"] = progress.lastPageRead;
        fields["read"] = progress.isRead;
        fields["ok"] = ok;
        app::testhooks::write("progress", fields);
    };
    return options;
}

brls::Label* makeLabel(float fontSize, NVGcolor color) {
    auto* label = new brls::Label();
    label->setFontSize(fontSize);
    label->setTextColor(color);
    // Under the focused page view borealis would scroll any label whose text is a hair wider than its box, forever (a
    // running animation: the app would never be idle). The reader's texts fit; they never scroll.
    label->setAutoAnimate(false);
    return label;
}

brls::Button* makeButton(const std::string& text, std::function<void()> action) {
    auto* button = new brls::Button();
    button->setText(text);
    button->setWidth(kButtonWidth);
    button->setHeight(kBarHeight);
    button->registerClickAction([action = std::move(action)](brls::View*) {
        brls::sync(action);  // after the tap is done (the action may close this screen)
        return true;
    });
    return button;
}

}  // namespace

ReaderActivity::ReaderActivity(app::AppContext& context, core::Manga manga, std::vector<core::Chapter> chapters,
                               int chapterId, int page, std::function<void()> onClosed)
    : context_(context),
      model_(context.client, context.images, manga, std::move(chapters),
             core::readerSettingsFor(context.config.config().reader, manga.id), optionsFor(context)),
      startChapter_(chapterId),
      startPage_(page),
      onClosed_(std::move(onClosed)),
      background_(context.config.config().reader.background),
      showPageNumber_(context.config.config().reader.showPageNumber),
      orientation_(context.config.config().reader.orientation),
      lastPortrait_(context.config.config().reader.lastPortrait),
      zoomOnTurn_(context.config.config().reader.zoomOnTurn) {}

ReaderActivity::~ReaderActivity() {
    if (subscribed_) {
        brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription_);
    }
    if (view_ != nullptr) {
        view_->onTap = nullptr;
        view_->onSwipe = nullptr;
        view_->onFrame = nullptr;
        view_->onDrawn = nullptr;
        view_->onGesture = nullptr;
    }
    app::testhooks::setUiBusy(false);
    model_.setListener({});
    platform::keepScreenOn(false);
}

brls::View* ReaderActivity::createContentView() {
    build();
    return view_;
}

void ReaderActivity::build() {
    view_ = new ReaderView(context_.textures);
    view_->setWidth(brls::Application::contentWidth);
    view_->setHeight(brls::Application::contentHeight);
    handheld_ = platform::handheldMode();
    view_->setOrientation(core::effectiveOrientation(orientation_, handheld_),
                          {brls::Application::contentWidth, brls::Application::contentHeight});
    brls::Box* overlay = view_->overlay();  // everything below goes here: upright in either orientation
    // A button works by touch in both orientations: borealis runs it in landscape, the view's mapped taps in portrait.
    auto button = [this](const std::string& text, std::function<void()> action) {
        brls::Button* made = makeButton(text, action);
        view_->addButton(made, std::move(action));
        return made;
    };

    // The message where there is no page: loading, an error with Retry, the screens before and after the chapter.
    message_ = new brls::Box(brls::Axis::COLUMN);
    message_->setAlignItems(brls::AlignItems::CENTER);
    // Widths: layoutForScreen().
    messageTitle_ = makeLabel(26, kBarText);
    messageTitle_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    message_->addView(messageTitle_);
    messageText_ = makeLabel(18, kBarText);
    messageText_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    messageText_->setMarginTop(14);
    message_->addView(messageText_);
    retryButton_ = button("reader/retry"_i18n, [this] { retry(); });
    retryButton_->setMarginTop(28);
    message_->addView(retryButton_);
    overlay->addView(message_);

    // The page number, bottom centre.
    pageNumberBox_ = new brls::Box(brls::Axis::ROW);
    pageNumberBox_->setPositionType(brls::PositionType::ABSOLUTE);
    pageNumberBox_->setPositionBottom(10);
    pageNumberBox_->setPositionLeft(0);
    pageNumberBox_->setJustifyContent(brls::JustifyContent::CENTER);
    auto* pill = new brls::Box(brls::Axis::ROW);
    pill->setBackgroundColor(kPageNumberBackground);
    pill->setCornerRadius(12);
    pill->setPadding(4, 14, 4, 14);
    pageNumber_ = makeLabel(16, kBarText);
    pill->addView(pageNumber_);
    pageNumberBox_->addView(pill);
    overlay->addView(pageNumberBox_);

    // The menu: the title on top, Close / page / Rotate / Settings at the bottom.
    topBar_ = new brls::Box(brls::Axis::ROW);
    topBar_->setPositionType(brls::PositionType::ABSOLUTE);
    topBar_->setPositionTop(0);
    topBar_->setPositionLeft(0);
    topBar_->setHeight(kBarHeight);
    topBar_->setAlignItems(brls::AlignItems::CENTER);
    topBar_->setBackgroundColor(kBarColor);
    barTitle_ = makeLabel(20, kBarText);
    barTitle_->setSingleLine(true);
    topBar_->addView(barTitle_);
    overlay->addView(topBar_);

    bottomBar_ = new brls::Box(brls::Axis::ROW);
    bottomBar_->setPositionType(brls::PositionType::ABSOLUTE);
    bottomBar_->setPositionBottom(0);
    bottomBar_->setPositionLeft(0);
    bottomBar_->setHeight(kBottomBarHeight);
    bottomBar_->setAlignItems(brls::AlignItems::CENTER);
    bottomBar_->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    bottomBar_->setBackgroundColor(kBarColor);
    barButtons_.push_back(button("reader/close"_i18n, [this] { close(); }));
    bottomBar_->addView(barButtons_.back());
    barPage_ = makeLabel(20, kBarText);
    bottomBar_->addView(barPage_);
    rotateButton_ = button("reader/rotate"_i18n, [this] { rotate(); });
    barButtons_.push_back(rotateButton_);
    bottomBar_->addView(rotateButton_);
    barButtons_.push_back(button("reader/settings"_i18n, [this] { openSettings(); }));
    bottomBar_->addView(barButtons_.back());
    overlay->addView(bottomBar_);

    // Controller. L/R turn pages (mirrored for right to left; also zoomed). The D-pad and the sticks are read once a frame
    // (pollControls, WP-13): they turn with the screen in portrait (core::readerArrow / readerStick), a turn needs a fresh
    // press (holding never turns more than one page), and zoomed they move around the page. borealis's NAV buttons ("D-pad
    // or either stick") are still taken here, doing nothing, so they never move the focus. ZR / ZL zoom in and out a step,
    // repeating while held.
    for (const brls::ControllerButton nav :
         {brls::BUTTON_NAV_LEFT, brls::BUTTON_NAV_RIGHT, brls::BUTTON_NAV_UP, brls::BUTTON_NAV_DOWN}) {
        view_->registerAction("", nav, [](brls::View*) { return true; }, true, true);
    }
    view_->registerAction("", brls::BUTTON_LB, [this](brls::View*) { turn(core::Side::Left); return true; }, true);
    view_->registerAction("", brls::BUTTON_RB, [this](brls::View*) { turn(core::Side::Right); return true; }, true);
    view_->registerAction("reader/zoom_in"_i18n, brls::BUTTON_RT, [this](brls::View*) { zoomStep(1); return true; }, true,
                          true);
    view_->registerAction("reader/zoom_out"_i18n, brls::BUTTON_LT, [this](brls::View*) { zoomStep(-1); return true; },
                          true, true);
    view_->registerAction("reader/rotate"_i18n, brls::BUTTON_Y, [this](brls::View*) { rotate(); return true; });
    view_->registerAction("reader/menu"_i18n, brls::BUTTON_A, [this](brls::View*) { toggleMenu(); return true; });
    view_->registerAction("reader/retry"_i18n, brls::BUTTON_X, [this](brls::View*) { retry(); return true; });
    view_->registerAction("reader/close"_i18n, brls::BUTTON_B, [this](brls::View*) { close(); return true; });
    // PLUS quits the app everywhere (D42), so the reader settings are on MINUS.
    view_->registerAction("reader/settings"_i18n, brls::BUTTON_BACK, [this](brls::View*) { openSettings(); return true; });

    // Touch (D16).
    view_->onTap = [this](core::TapArea area) {
        if (menuShown_ || area == core::TapArea::Middle) {
            toggleMenu();  // with the menu open, a tap on the page only closes it
        } else {
            turn(area == core::TapArea::Left ? core::Side::Left : core::Side::Right);
        }
    };
    view_->onSwipe = [this](core::Side side) { turn(side); };
    view_->onFrame = [this] {
        pollControls();  // before the page is drawn, so a press shows in this frame
        settleZoom();
    };
    view_->onDrawn = [this] {
        followScreen();    // docked or undocked, the screen's size
        model_.onFrame();  // progress, when it's due
        report();
        // Test builds: not idle while a middle tap waits, a zoom waits to decode, or the page moves by itself.
        app::testhooks::setUiBusy(seenZoomRevision_ != appliedZoomRevision_ || model_.zoomLoading() || view_->gliding() ||
                                  view_->pinching() || view_->tapWaiting() || stickPushing_ || !gestureKind_.empty());
    };
    view_->onGesture = [this](const char* kind, bool started) {
        if (started) {
            gestureBegin(kind);
        } else {
            gestureEnd(kind);
        }
    };
    view_->setMaxZoom(context_.profile.maxZoom);
    view_->setKeepZoomOnTurn(zoomOnTurn_ == core::ZoomOnTurn::Keep);
    layoutForScreen();
}

void ReaderActivity::followScreen() {
    const bool handheld = platform::handheldMode();
    const core::ReaderOrientation shown = core::effectiveOrientation(orientation_, handheld);
    if (shown != view_->orientation() || handheld != handheld_) {
        handheld_ = handheld;
        // In portrait nothing but the page may have the focus (its highlight is hidden; a button's would be drawn unturned).
        // Only a reader button's focus moves: a settings menu on top keeps its own.
        brls::View* focus = brls::Application::getCurrentFocus();
        for (brls::View* v = focus; v != nullptr && focus != view_; v = v->getParent()) {
            if (v == view_) {
                brls::Application::giveFocus(view_);
                break;
            }
        }
        view_->setOrientation(shown, {brls::Application::contentWidth, brls::Application::contentHeight});
        layoutForScreen();
        brls::Logger::info("reader: orientation {} (saved {}, {})", core::readerOrientationName(shown),
                           core::readerOrientationName(orientation_), handheld ? "handheld" : "docked");
    }
    const int width = static_cast<int>(brls::Application::windowWidth);
    const int height = static_cast<int>(brls::Application::windowHeight);
    if (width > 0 && height > 0) {
        model_.setScreen(core::readerScreen(shown, core::ImageSize{width, height}));  // pages decode for it
    }
}

void ReaderActivity::layoutForScreen() {
    const core::SizeF screen = view_->readerScreen();
    const bool portrait = core::isPortrait(view_->orientation());
    const float padding = portrait ? kPortraitBarPadding : kBarPadding;
    const float messageWidth = std::min(kMessageWidth, screen.width - 2 * kMessageMargin);
    message_->setWidth(messageWidth);
    messageTitle_->setWidth(messageWidth);
    messageText_->setWidth(messageWidth);
    pageNumberBox_->setWidth(screen.width);
    topBar_->setWidth(screen.width);
    topBar_->setPadding(0, padding, 0, padding);
    barTitle_->setWidth(screen.width - 2 * padding);
    bottomBar_->setWidth(screen.width);
    bottomBar_->setPadding(0, padding, 0, padding);
    for (brls::Button* barButton : barButtons_) {
        barButton->setWidth(portrait ? kPortraitButtonWidth : kButtonWidth);
    }
    rotateButton_->setVisibility(handheld_ ? brls::Visibility::VISIBLE : brls::Visibility::GONE);  // docked: landscape
}

bool ReaderActivity::readerHasFocus() const {
    // The D-pad and sticks act only while the reader is the screen in use (not under the settings menu).
    for (brls::View* v = brls::Application::getCurrentFocus(); v != nullptr; v = v->getParent()) {
        if (v == view_) {
            return true;
        }
    }
    return false;
}

void ReaderActivity::pollControls() {
    const std::int64_t now = app::appClockMs();
    const float seconds =
        lastFrameMs_ < 0 ? 0.0f : static_cast<float>(std::min(now - lastFrameMs_, kMaxStickStepMs)) / 1000.0f;
    lastFrameMs_ = now;
    gestureFrame(now);
    const brls::ControllerState& state = brls::Application::getControllerState();
    const bool active = !closing_ && readerHasFocus();
    for (std::size_t i = 0; i < std::size(kArrows); ++i) {
        ArrowHold& hold = dpad_[i];
        const bool pressed = state.buttons[kDpad[i]];
        if (!pressed) {
            hold = ArrowHold{};
            continue;
        }
        const bool fresh = !hold.down;
        hold.down = true;
        if (fresh) {
            hold.nextRepeatMs = now + kRepeatFirstMs;
        } else if (now >= hold.nextRepeatMs) {
            hold.nextRepeatMs = now + kRepeatMs;
        } else {
            continue;
        }
        if (active) {
            arrowPress(kArrows[i], fresh, hold);
        }
    }
    if (active) {
        pollStick(seconds);
    }
}

void ReaderActivity::arrowPress(core::Arrow pressed, bool fresh, ArrowHold& hold) {
    if (hold.latched) {
        return;  // this press turned a page: one press, one page
    }
    const core::Arrow arrow = core::readerArrow(view_->orientation(), pressed);
    const bool zoomed = core::isZoomed(view_->zoom());
    switch (core::arrowEffect(arrow, zoomed, view_->canMove(arrow), fresh)) {
        case core::ArrowEffect::Turn:
            hold.latched = true;
            turn(*core::arrowSide(arrow));
            break;
        case core::ArrowEffect::Move:
            if (zoomed) {
                view_->glideBy(core::arrowMove(arrow, view_->readerScreen()));
            } else {
                const float scroll = view_->readerScreen().height * kScrollShare;
                view_->scrollBy(arrow == core::Arrow::Up ? -scroll : scroll);
            }
            break;
        case core::ArrowEffect::Nothing:
            break;
    }
}

void ReaderActivity::pollStick(float seconds) {
    // Either stick: the one pushed further (in portrait the one on the bottom Joy-Con is in reach).
    const brls::ControllerState& state = brls::Application::getControllerState();
    const core::PointF left{state.axes[brls::LEFT_X], state.axes[brls::LEFT_Y]};
    const core::PointF right{state.axes[brls::RIGHT_X], state.axes[brls::RIGHT_Y]};
    const core::PointF raw = std::hypot(left.x, left.y) >= std::hypot(right.x, right.y) ? left : right;
    const core::PointF stick = core::readerStick(view_->orientation(), raw);
    if (std::hypot(stick.x, stick.y) <= kStickDeadZone) {
        // Back at rest: the push is over.
        if (stickPanning_) {
            view_->finishPan();
            gestureEnd("stick");
        }
        stickPushing_ = false;
        stickPanning_ = false;
        stickLatched_ = false;
        return;
    }
    if (stickLatched_) {
        return;  // it turned a page: nothing until it is let go
    }
    if (!stickPushing_) {
        stickPushing_ = true;
        stickEdgeLeft_ = !view_->canMove(core::Arrow::Left);
        stickEdgeRight_ = !view_->canMove(core::Arrow::Right);
    }
    const bool zoomed = core::isZoomed(view_->zoom());
    const std::optional<core::Arrow> pushed = core::stickArrow(stick);
    const std::optional<core::Side> side = pushed ? core::arrowSide(*pushed) : std::nullopt;
    // Not zoomed, a push past half way sideways is a D-pad press (it turns). Zoomed, only a push that began with the page
    // already at its edge that way turns, like a fresh D-pad press at the edge.
    if (side && (!zoomed || (*side == core::Side::Left ? stickEdgeLeft_ : stickEdgeRight_))) {
        stickLatched_ = true;
        if (stickPanning_) {
            view_->finishPan();
            gestureEnd("stick");
            stickPanning_ = false;
        }
        turn(*side);
        return;
    }
    // Zoomed: a smooth pan, faster the further pushed. Not zoomed: only up and down (a tall page scrolls).
    const core::PointF push = zoomed ? stick : core::PointF{0, stick.y};
    const core::PointF move = core::stickMove(push, view_->readerScreen(), seconds, 0.6f, kStickDeadZone);
    if (view_->panBy(move) && !stickPanning_) {
        stickPanning_ = true;
        gestureBegin("stick");
    }
}

void ReaderActivity::zoomStep(int direction) {
    if (!view_->hasPage()) {
        return;
    }
    const unsigned before = view_->zoomRevision();
    view_->zoomStep(direction);
    if (view_->zoomRevision() != before) {
        gestureBegin("zoom_step");
        gestureEndAfter_ = 2;  // the frame that draws it, and the one after
    }
}

void ReaderActivity::settleZoom() {
    // The view's zoom changed: once it has stayed for kZoomSettleMs (and no fingers pinch), the page on screen decodes
    // again for it (or goes back to its fit image).
    const std::int64_t now = app::appClockMs();
    if (view_->zoomRevision() != seenZoomRevision_) {
        seenZoomRevision_ = view_->zoomRevision();
        zoomSeenMs_ = now;
        sharpMs_ = -1;
    }
    if (seenZoomRevision_ != appliedZoomRevision_ && !view_->pinching() && now - zoomSeenMs_ >= kZoomSettleMs) {
        appliedZoomRevision_ = seenZoomRevision_;
        model_.setPageZoom(view_->zoom());
    }
}

void ReaderActivity::gestureBegin(const std::string& kind) {
    if (!gestureKind_.empty()) {
        gestureEnd(gestureKind_);  // one at a time: a new one ends the last
    }
    gestureKind_ = kind;
    gestureLastMs_ = app::appClockMs();
    gestureEndAfter_ = 0;
    gestureFrames_.clear();
}

void ReaderActivity::gestureFrame(std::int64_t now) {
    if (gestureKind_.empty()) {
        return;
    }
    gestureFrames_.push_back(static_cast<int>(now - gestureLastMs_));
    gestureLastMs_ = now;
    if (gestureEndAfter_ > 0 && --gestureEndAfter_ == 0) {
        gestureEnd(gestureKind_);
    }
}

void ReaderActivity::gestureEnd(const std::string& kind) {
    if (gestureKind_ != kind || kind.empty()) {
        return;
    }
    // {"ev":"gesture"}: the frame intervals while it ran (WP-13 acceptance: p90 <= 17 ms, none over 34 ms on the console).
    std::vector<int> sorted = gestureFrames_;
    std::sort(sorted.begin(), sorted.end());
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["kind"] = kind;
    fields["frames"] = sorted.size();
    fields["p90Ms"] = sorted.empty() ? 0 : sorted[std::min(sorted.size() - 1, sorted.size() * 9 / 10)];
    fields["maxMs"] = sorted.empty() ? 0 : sorted.back();
    fields["zoom"] = std::round(view_->zoom() * 100) / 100;
    fields["x"] = static_cast<int>(std::lround(view_->pagePosition().x));
    fields["y"] = static_cast<int>(std::lround(view_->pagePosition().y));
    app::testhooks::write("gesture", fields);
    gestureKind_.clear();
    gestureFrames_.clear();
    gestureEndAfter_ = 0;
}

void ReaderActivity::rotate() {
    if (!handheld_) {
        return;  // docked: always landscape (the button is hidden then)
    }
    setOrientation(core::quickRotate(orientation_, lastPortrait_));
}

void ReaderActivity::setOrientation(core::ReaderOrientation orientation) {
    orientation_ = orientation;
    if (core::isPortrait(orientation)) {
        lastPortrait_ = orientation;
    }
    context_.config.setReaderOrientation(orientation_, lastPortrait_);
    followScreen();
}

void ReaderActivity::chooseOrientation() {
    std::vector<std::string> names;
    for (const char* key : kOrientationKeys) {
        names.push_back(brls::getStr(key));
    }
    auto* dropdown = new brls::Dropdown("reader/orientation"_i18n, names, [this](int selected) {
        if (selected < 0 || selected >= static_cast<int>(std::size(kOrientations))) {
            return;
        }
        setOrientation(kOrientations[selected]);
    }, indexOf(kOrientations, orientation_));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ReaderActivity::onContentAvailable() {
    applyLook();
    updateBars();
    platform::keepScreenOn(true);
    focusSubscription_ = brls::Application::getWindowFocusChangedEvent()->subscribe([this](bool focused) {
        if (!focused) {
            model_.saveNow();  // HOME or sleep: the progress goes out now
        }
    });
    subscribed_ = true;
    model_.setListener([this] { modelChanged(); });
    followScreen();  // pages decode for the orientation from the start
    brls::Logger::info("reader: manga {} chapter {} page {}", model_.manga().id, startChapter_, startPage_);
    model_.open(startChapter_, startPage_);
    modelChanged();
    reportScreen();
}

void ReaderActivity::onResume() {
    brls::Application::giveFocus(view_);
    reportScreen();
}

void ReaderActivity::reportScreen() {
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["mangaId"] = model_.manga().id;
    params["chapterId"] = model_.chapter().id;
    params["page"] = model_.state() == ReaderModel::State::Ready ? model_.position() : startPage_;
    app::testhooks::screen("Reader", params);
}

void ReaderActivity::modelChanged() {
    const bool pageReady = model_.state() == ReaderModel::State::Ready && model_.onPage() &&
                           model_.current().state == ReaderPage::State::Ready;
    // The page's key tells the view a sharper decode of the same page (WP-13) from a new page.
    const std::string key = std::to_string(model_.chapter().id) + "/" + std::to_string(model_.position());
    view_->showPage(pageReady ? model_.shownImage() : nullptr, model_.fit(), model_.direction(), key);
    updateMessage();
    updateBars();
}

void ReaderActivity::updateMessage() {
    std::string title;
    std::string text;
    bool retry = false;
    bool spinner = false;
    const std::string name = chapterName(model_.chapter());
    switch (model_.state()) {
        case ReaderModel::State::Loading:
            spinner = true;
            break;
        case ReaderModel::State::Failed:
            title = brls::getStr("reader/chapter_failed", name);
            text = errorText(model_.error());
            retry = true;
            break;
        case ReaderModel::State::Ready:
            if (model_.position() < 0) {
                const core::Chapter* previous = model_.previousChapter();
                title = brls::getStr("reader/start_of", name);
                text = previous ? brls::getStr("reader/previous", chapterName(*previous)) : "reader/no_previous"_i18n;
            } else if (!model_.onPage()) {
                const core::Chapter* next = model_.nextChapter();
                title = model_.pageCount() == 0 ? brls::getStr("reader/no_pages", name)
                                                : brls::getStr("reader/finished", name);
                text = next ? brls::getStr("reader/next", chapterName(*next)) : "reader/no_next"_i18n;
            } else {
                const ReaderPage& page = model_.current();
                if (page.state == ReaderPage::State::Failed) {
                    title = brls::getStr("reader/page_failed", model_.position() + 1);
                    text = imageErrorText(page.error);
                    retry = true;
                } else if (page.state == ReaderPage::State::Ready && view_->textureRefused()) {
                    title = brls::getStr("reader/page_failed", model_.position() + 1);
                    text = "reader/page_error_memory"_i18n;
                } else if (page.state != ReaderPage::State::Ready) {
                    spinner = true;
                }
            }
            break;
    }
    const bool shown = !title.empty() || !text.empty();
    // Never hide a view that has the focus (D42): the Retry button may have it after a tap.
    if (!retry && brls::Application::getCurrentFocus() == retryButton_) {
        brls::Application::giveFocus(view_);
    }
    message_->setVisibility(shown ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    messageTitle_->setText(title);
    messageText_->setText(text);
    messageText_->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    retryButton_->setVisibility(retry ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    view_->setSpinner(spinner);
}

void ReaderActivity::updateBars() {
    const std::string name = chapterName(model_.chapter());
    barTitle_->setText(model_.manga().title + "  -  " + name);
    std::string counter;
    if (model_.state() == ReaderModel::State::Ready && model_.onPage()) {
        counter = brls::getStr("reader/page_of", model_.position() + 1, model_.pageCount());
    }
    pageNumber_->setText(counter);
    barPage_->setText(counter);
    const bool number = showPageNumber_ && !menuShown_ && !counter.empty();
    pageNumberBox_->setVisibility(number ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (!menuShown_) {
        brls::View* focus = brls::Application::getCurrentFocus();
        for (brls::View* v = focus; v != nullptr; v = v->getParent()) {
            if (v == bottomBar_) {
                brls::Application::giveFocus(view_);  // never hide a view that has the focus (D42)
                break;
            }
        }
    }
    topBar_->setVisibility(menuShown_ ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    bottomBar_->setVisibility(menuShown_ ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    view_->setDoubleTapWait(!menuShown_);  // with the menu open any tap closes it at once (no double tap then)
}

void ReaderActivity::applyLook() {
    view_->setBackgroundColor(backgroundColor(background_));
    messageTitle_->setTextColor(textColor(background_));
    messageText_->setTextColor(textColor(background_));
}

void ReaderActivity::turn(core::Side side) {
    step(core::pageStep(side, model_.direction()));
}

void ReaderActivity::step(int delta) {
    const std::int64_t started = app::appClockMs();
    if (model_.step(delta)) {
        turnStartMs_ = started;
        turnReady_ = model_.onPage() && model_.current().state == ReaderPage::State::Ready;
    }
}

void ReaderActivity::toggleMenu() {
    menuShown_ = !menuShown_;
    updateBars();
}

void ReaderActivity::retry() {
    brls::Application::giveFocus(view_);
    if (model_.state() == ReaderModel::State::Failed) {
        ++retries_;
        model_.retry();
    } else if (model_.onPage() && model_.current().state == ReaderPage::State::Failed) {
        ++retries_;
        model_.retryPage();
    }
}

void ReaderActivity::close() {
    if (closing_) {
        return;
    }
    closing_ = true;
    platform::keepScreenOn(false);
    model_.close(onClosed_);  // the details screen reloads its chapters once the progress is saved
    brls::Application::popActivity();
}

void ReaderActivity::openSettings() {
    brls::Application::giveFocus(view_);
    const std::vector<std::string> entries = {
        brls::getStr("reader/setting_direction", brls::getStr(kDirectionKeys[indexOf(kDirections, model_.direction())])),
        brls::getStr("reader/setting_fit", brls::getStr(kFitKeys[indexOf(kFits, model_.fit())])),
        brls::getStr("reader/setting_background", brls::getStr(kBackgroundKeys[indexOf(kBackgrounds, background_)])),
        showPageNumber_ ? "reader/setting_page_number_on"_i18n : "reader/setting_page_number_off"_i18n,
        brls::getStr("reader/setting_orientation",
                     brls::getStr(kOrientationKeys[indexOf(kOrientations, orientation_)])),
        brls::getStr("reader/setting_zoom_on_turn", brls::getStr(kZoomOnTurnKeys[indexOf(kZoomOnTurns, zoomOnTurn_)])),
    };
    auto* dropdown = new brls::Dropdown("reader/settings"_i18n, entries, [this](int selected) {
        // Open the next menu after this one has closed.
        brls::sync([this, selected] {
            switch (selected) {
                case 0:
                    chooseDirection();
                    break;
                case 1:
                    chooseFit();
                    break;
                case 2:
                    chooseBackground();
                    break;
                case 3:
                    showPageNumber_ = !showPageNumber_;
                    context_.config.setReaderLook(background_, showPageNumber_);
                    updateBars();
                    break;
                case 4:
                    chooseOrientation();
                    break;
                case 5:
                    chooseZoomOnTurn();
                    break;
                default:
                    break;
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ReaderActivity::chooseDirection() {
    std::vector<std::string> names;
    for (const char* key : kDirectionKeys) {
        names.push_back(brls::getStr(key));
    }
    auto* dropdown = new brls::Dropdown("reader/direction"_i18n, names, [this](int selected) {
        if (selected < 0 || selected >= static_cast<int>(std::size(kDirections))) {
            return;
        }
        model_.setDirection(kDirections[selected]);
        context_.config.setMangaReader(model_.settings());
    }, indexOf(kDirections, model_.direction()));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ReaderActivity::chooseFit() {
    std::vector<std::string> names;
    for (const char* key : kFitKeys) {
        names.push_back(brls::getStr(key));
    }
    auto* dropdown = new brls::Dropdown("reader/fit"_i18n, names, [this](int selected) {
        if (selected < 0 || selected >= static_cast<int>(std::size(kFits))) {
            return;
        }
        model_.setFit(kFits[selected]);
        context_.config.setMangaReader(model_.settings());
    }, indexOf(kFits, model_.fit()));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ReaderActivity::chooseZoomOnTurn() {
    std::vector<std::string> names;
    for (const char* key : kZoomOnTurnKeys) {
        names.push_back(brls::getStr(key));
    }
    auto* dropdown = new brls::Dropdown("reader/zoom_on_turn"_i18n, names, [this](int selected) {
        if (selected < 0 || selected >= static_cast<int>(std::size(kZoomOnTurns))) {
            return;
        }
        zoomOnTurn_ = kZoomOnTurns[selected];
        view_->setKeepZoomOnTurn(zoomOnTurn_ == core::ZoomOnTurn::Keep);
        context_.config.setReaderZoomOnTurn(zoomOnTurn_);
    }, indexOf(kZoomOnTurns, zoomOnTurn_));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ReaderActivity::chooseBackground() {
    std::vector<std::string> names;
    for (const char* key : kBackgroundKeys) {
        names.push_back(brls::getStr(key));
    }
    auto* dropdown = new brls::Dropdown("reader/background"_i18n, names, [this](int selected) {
        if (selected < 0 || selected >= static_cast<int>(std::size(kBackgrounds))) {
            return;
        }
        background_ = kBackgrounds[selected];
        context_.config.setReaderLook(background_, showPageNumber_);
        applyLook();
    }, indexOf(kBackgrounds, background_));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ReaderActivity::report() {
    // One "reader" event whenever what the screen shows has settled into something new: a page drawn or failed, a chapter
    // that failed, the screens before and after the chapter, the menu, the settings. Nothing while something on screen is
    // still loading. Test builds only write it; the check is cheap, the JSON is built only when something may have changed.
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    std::string shown;
    if (model_.state() == ReaderModel::State::Loading) {
        return;
    }
    if (model_.state() == ReaderModel::State::Failed) {
        shown = "chapter_error";
    } else if (model_.position() < 0) {
        shown = "start";
    } else if (!model_.onPage()) {
        shown = "end";
    } else if (model_.current().state == ReaderPage::State::Failed || view_->textureRefused()) {
        shown = "page_error";
    } else if (model_.current().state == ReaderPage::State::Ready && view_->hasPage()) {
        shown = "page";
    } else {
        return;  // loading
    }
    const int page = model_.position();
    // WP-13: a sharper decode drawn for the first time: how long after the zoom changed (the 150 ms wait included).
    const image::ImagePtr& drawnImage = view_->image();
    // Against the box the zoom on screen wants (not the model's, which follows only once the zoom has settled).
    const bool sharp =
        drawnImage && core::isSharp(drawnImage->size(), drawnImage->sourceSize(), model_.boxForZoom(view_->zoom()));
    if (view_->imageRevision() != seenImageRevision_) {
        seenImageRevision_ = view_->imageRevision();
        if (sharp && core::isZoomed(view_->zoom())) {
            sharpMs_ = app::appClockMs() - zoomSeenMs_;
        }
    }
    const core::ImageSize decoded = drawnImage ? drawnImage->size() : core::ImageSize{};
    const std::string key = std::to_string(model_.chapter().id) + "/" + std::to_string(page) + "/" + shown + "/" +
                            std::to_string(static_cast<int>(model_.fit())) + "/" +
                            std::to_string(static_cast<int>(model_.direction())) + "/" + (menuShown_ ? "m" : "") +
                            (showPageNumber_ ? "n" : "") + std::to_string(static_cast<int>(background_)) +
                            (model_.chapterRead() ? "r" : "") + "/" + std::to_string(view_->pageRevision()) + "/" +
                            std::to_string(retries_) + "/" + std::to_string(static_cast<int>(view_->orientation())) +
                            std::to_string(static_cast<int>(orientation_)) + "/" + std::to_string(view_->moveRevision()) +
                            "/" + std::to_string(view_->zoomRevision()) + "/" + std::to_string(decoded.width) + "x" +
                            std::to_string(decoded.height) + "/" + std::to_string(static_cast<int>(zoomOnTurn_));
    if (key == reported_) {
        return;
    }
    reported_ = key;

    fields["chapterId"] = model_.chapter().id;
    fields["chapter"] = model_.chapter().sourceOrder;
    fields["page"] = page;
    fields["of"] = model_.pageCount();
    fields["shown"] = shown;
    fields["direction"] = std::string(core::readingDirectionName(model_.direction()));
    fields["fit"] = std::string(core::pageFitName(model_.fit()));
    fields["menu"] = menuShown_;
    fields["pageNumber"] = showPageNumber_;
    fields["background"] = std::string(core::readerBackgroundName(background_));
    fields["read"] = model_.chapterRead();
    // WP-22: the orientation in effect, the saved choice, and the reader's screen (page sizes below are in its units).
    fields["orientation"] = std::string(core::readerOrientationName(view_->orientation()));
    fields["orientationSetting"] = std::string(core::readerOrientationName(orientation_));
    fields["screenW"] = static_cast<int>(view_->readerScreen().width);
    fields["screenH"] = static_cast<int>(view_->readerScreen().height);
    if (shown == "page") {
        const image::ImagePtr& image = model_.current().image;
        fields["spread"] = core::isSpread(image->sourceSize());
        fields["w"] = static_cast<int>(std::lround(view_->pageSize().width));
        fields["h"] = static_cast<int>(std::lround(view_->pageSize().height));
        fields["srcW"] = image->sourceSize().width;
        fields["srcH"] = image->sourceSize().height;
        // Where the page's corner is (a drag or a D-pad scroll moved it: reported once it ended).
        fields["x"] = static_cast<int>(std::lround(view_->pagePosition().x));
        fields["y"] = static_cast<int>(std::lround(view_->pagePosition().y));
        // WP-13: the zoom (1 = the fit), the decoded image drawn, whether it has the detail the zoom wants (as far as the
        // source and the cap allow), and once, how long the sharper decode took to show.
        fields["zoom"] = std::round(view_->zoom() * 100) / 100;
        fields["decW"] = decoded.width;
        fields["decH"] = decoded.height;
        fields["sharp"] = sharp;
        if (sharpMs_ >= 0) {
            fields["sharpMs"] = sharpMs_;
            sharpMs_ = -1;
        }
    } else if (shown == "page_error") {
        const char* kind = view_->textureRefused() ? "texture" : image::imageErrorKindName(model_.current().error.kind);
        fields["error"] = kind;
        app::testhooks::error("reader", kind, model_.current().error.message);
    } else if (shown == "chapter_error") {
        fields["error"] = std::string(api::apiErrorKindName(model_.error().kind));
        app::testhooks::error("reader", api::apiErrorKindName(model_.error().kind), model_.error().message);
    }
    fields["retries"] = retries_;
    if (turnStartMs_ >= 0) {
        // From the key or tap to the first frame that shows the result (D17, WP-11: < 150 ms when the page was prefetched).
        fields["turnMs"] = app::appClockMs() - turnStartMs_;
        fields["ready"] = turnReady_;
        turnStartMs_ = -1;
    }
    fields["zoomOnTurn"] = std::string(core::zoomOnTurnName(zoomOnTurn_));
    fields["texUsed"] = context_.textures.used();
    fields["memUsed"] = platform::runtimeInfo().usedMem;
    app::testhooks::write("reader", fields);
}

}  // namespace snx::ui
