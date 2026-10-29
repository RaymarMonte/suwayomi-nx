#include "ui/reader/ReaderActivity.hpp"

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
constexpr float kScrollShare = 0.5f;     // D-pad up/down moves a tall page by half a screen
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
      showPageNumber_(context.config.config().reader.showPageNumber) {}

ReaderActivity::~ReaderActivity() {
    if (subscribed_) {
        brls::Application::getWindowFocusChangedEvent()->unsubscribe(focusSubscription_);
    }
    if (view_ != nullptr) {
        view_->onTap = nullptr;
        view_->onSwipe = nullptr;
        view_->onDrawn = nullptr;
    }
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
    view_->setAxis(brls::Axis::COLUMN);
    view_->setJustifyContent(brls::JustifyContent::CENTER);
    view_->setAlignItems(brls::AlignItems::CENTER);

    // The message where there is no page: loading, an error with Retry, the screens before and after the chapter.
    message_ = new brls::Box(brls::Axis::COLUMN);
    message_->setAlignItems(brls::AlignItems::CENTER);
    message_->setWidth(kMessageWidth);
    messageTitle_ = makeLabel(26, kBarText);
    messageTitle_->setWidth(kMessageWidth);
    messageTitle_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    message_->addView(messageTitle_);
    messageText_ = makeLabel(18, kBarText);
    messageText_->setWidth(kMessageWidth);
    messageText_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    messageText_->setMarginTop(14);
    message_->addView(messageText_);
    retryButton_ = makeButton("reader/retry"_i18n, [this] { retry(); });
    retryButton_->setMarginTop(28);
    message_->addView(retryButton_);
    view_->addView(message_);

    // The page number, bottom centre.
    pageNumberBox_ = new brls::Box(brls::Axis::ROW);
    pageNumberBox_->setPositionType(brls::PositionType::ABSOLUTE);
    pageNumberBox_->setPositionBottom(10);
    pageNumberBox_->setPositionLeft(0);
    pageNumberBox_->setWidth(brls::Application::contentWidth);
    pageNumberBox_->setJustifyContent(brls::JustifyContent::CENTER);
    auto* pill = new brls::Box(brls::Axis::ROW);
    pill->setBackgroundColor(kPageNumberBackground);
    pill->setCornerRadius(12);
    pill->setPadding(4, 14, 4, 14);
    pageNumber_ = makeLabel(16, kBarText);
    pill->addView(pageNumber_);
    pageNumberBox_->addView(pill);
    view_->addView(pageNumberBox_);

    // The menu: the title on top, Close / page / Settings at the bottom.
    topBar_ = new brls::Box(brls::Axis::ROW);
    topBar_->setPositionType(brls::PositionType::ABSOLUTE);
    topBar_->setPositionTop(0);
    topBar_->setPositionLeft(0);
    topBar_->setWidth(brls::Application::contentWidth);
    topBar_->setHeight(kBarHeight);
    topBar_->setAlignItems(brls::AlignItems::CENTER);
    topBar_->setPadding(0, 32, 0, 32);
    topBar_->setBackgroundColor(kBarColor);
    barTitle_ = makeLabel(20, kBarText);
    barTitle_->setSingleLine(true);
    barTitle_->setWidth(brls::Application::contentWidth - 64);
    topBar_->addView(barTitle_);
    view_->addView(topBar_);

    bottomBar_ = new brls::Box(brls::Axis::ROW);
    bottomBar_->setPositionType(brls::PositionType::ABSOLUTE);
    bottomBar_->setPositionBottom(0);
    bottomBar_->setPositionLeft(0);
    bottomBar_->setWidth(brls::Application::contentWidth);
    bottomBar_->setHeight(kBottomBarHeight);
    bottomBar_->setAlignItems(brls::AlignItems::CENTER);
    bottomBar_->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    bottomBar_->setPadding(0, 32, 0, 32);
    bottomBar_->setBackgroundColor(kBarColor);
    bottomBar_->addView(makeButton("reader/close"_i18n, [this] { close(); }));
    barPage_ = makeLabel(20, kBarText);
    bottomBar_->addView(barPage_);
    bottomBar_->addView(makeButton("reader/settings"_i18n, [this] { openSettings(); }));
    view_->addView(bottomBar_);

    // Controller. Left/right turn pages (mirrored for right to left): the D-pad (and the sticks, borealis's NAV buttons)
    // and L/R. Their actions come before borealis's focus moves, so a page turn never moves the focus.
    view_->registerAction("", brls::BUTTON_NAV_LEFT, [this](brls::View*) { turn(core::Side::Left); return true; }, true);
    view_->registerAction("", brls::BUTTON_NAV_RIGHT, [this](brls::View*) { turn(core::Side::Right); return true; }, true);
    view_->registerAction("", brls::BUTTON_LB, [this](brls::View*) { turn(core::Side::Left); return true; }, true);
    view_->registerAction("", brls::BUTTON_RB, [this](brls::View*) { turn(core::Side::Right); return true; }, true);
    view_->registerAction("", brls::BUTTON_NAV_UP, [this](brls::View*) {
        view_->scrollBy(-brls::Application::contentHeight * kScrollShare);
        return true;
    }, true, true);
    view_->registerAction("", brls::BUTTON_NAV_DOWN, [this](brls::View*) {
        view_->scrollBy(brls::Application::contentHeight * kScrollShare);
        return true;
    }, true, true);
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
    view_->onDrawn = [this] {
        model_.onFrame();  // progress, when it's due
        report();
    };
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
    view_->showPage(pageReady ? model_.current().image : nullptr, model_.fit(), model_.direction());
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
    const std::string key = std::to_string(model_.chapter().id) + "/" + std::to_string(page) + "/" + shown + "/" +
                            std::to_string(static_cast<int>(model_.fit())) + "/" +
                            std::to_string(static_cast<int>(model_.direction())) + "/" + (menuShown_ ? "m" : "") +
                            (showPageNumber_ ? "n" : "") + std::to_string(static_cast<int>(background_)) +
                            (model_.chapterRead() ? "r" : "") + "/" + std::to_string(view_->pageRevision()) + "/" +
                            std::to_string(retries_);
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
    if (shown == "page") {
        const image::ImagePtr& image = model_.current().image;
        fields["spread"] = core::isSpread(image->sourceSize());
        fields["w"] = static_cast<int>(std::lround(view_->pageSize().width));
        fields["h"] = static_cast<int>(std::lround(view_->pageSize().height));
        fields["srcW"] = image->sourceSize().width;
        fields["srcH"] = image->sourceSize().height;
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
    fields["memUsed"] = platform::runtimeInfo().usedMem;
    app::testhooks::write("reader", fields);
}

}  // namespace snx::ui
