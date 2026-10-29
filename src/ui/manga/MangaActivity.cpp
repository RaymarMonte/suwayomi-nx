#include "ui/manga/MangaActivity.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "app/TestHooks.hpp"
#include "ui/components/TextFit.hpp"
#include "ui/reader/ReaderActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr float kCoverWidth = 120;  // the library grid's size, so the cover cache already has it
constexpr float kCoverHeight = 180;
constexpr float kInfoWidth = 256;
constexpr float kDescriptionWidth = 376;
constexpr int kTitleLines = 3;
constexpr int kGenreLines = 2;
constexpr int kDescriptionLines = 5;  // until it is opened
constexpr float kLineHeight = 1.25f;

// The chapter list: one column of rows, 80 px high (D16: over the 72 px touch minimum).
constexpr float kRowWidth = 800;
constexpr float kRowHeight = 80;
constexpr GridMetrics kListMetrics{kRowWidth, kRowHeight, 4, 8};
constexpr float kMenuWidth = 72;  // the "..." at the end of a row
constexpr float kRowTextWidth = kRowWidth - 6 - 14 - 10 - 14 - kMenuWidth - 8;

const ChapterSort kSorts[] = {ChapterSort::NewestFirst, ChapterSort::OldestFirst};
const char* const kSortKeys[] = {"manga/sort_newest", "manga/sort_oldest"};
const ChapterFilter kFilters[] = {ChapterFilter::All, ChapterFilter::Unread, ChapterFilter::Bookmarked};
const char* const kFilterKeys[] = {"manga/filter_all", "manga/filter_unread", "manga/filter_bookmarked"};
// Secondary text (author, a row's details): lighter than the theme's "disabled" grey, which is hard to read there; it
// reads on the dark and the light background alike.
const NVGcolor kSecondaryText = nvgRGB(150, 150, 150);

NVGcolor themeColor(const char* name) {
    return brls::Application::getTheme()[name];
}

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

std::string statusText(core::MangaStatus status) {
    switch (status) {
        case core::MangaStatus::Ongoing:
            return "manga/status_ongoing"_i18n;
        case core::MangaStatus::Completed:
            return "manga/status_completed"_i18n;
        case core::MangaStatus::Licensed:
            return "manga/status_licensed"_i18n;
        case core::MangaStatus::PublishingFinished:
            return "manga/status_publishing_finished"_i18n;
        case core::MangaStatus::Cancelled:
            return "manga/status_cancelled"_i18n;
        case core::MangaStatus::OnHiatus:
            return "manga/status_on_hiatus"_i18n;
        case core::MangaStatus::Unknown:
            break;
    }
    return "manga/status_unknown"_i18n;
}

const char* actionKey(MangaModel::Action action) {
    switch (action) {
        case MangaModel::Action::MarkRead:
            return "manga/mark_read";
        case MangaModel::Action::MarkUnread:
            return "manga/mark_unread";
        case MangaModel::Action::Bookmark:
            return "manga/bookmark";
        case MangaModel::Action::RemoveBookmark:
            return "manga/remove_bookmark";
        case MangaModel::Action::MarkPreviousRead:
            return "manga/mark_previous_read";
        case MangaModel::Action::MarkPreviousUnread:
            break;
    }
    return "manga/mark_previous_unread";
}

// The names in "chapter_action" test events.
const char* actionName(MangaModel::Action action) {
    switch (action) {
        case MangaModel::Action::MarkRead:
            return "mark_read";
        case MangaModel::Action::MarkUnread:
            return "mark_unread";
        case MangaModel::Action::Bookmark:
            return "bookmark";
        case MangaModel::Action::RemoveBookmark:
            return "remove_bookmark";
        case MangaModel::Action::MarkPreviousRead:
            return "mark_previous_read";
        case MangaModel::Action::MarkPreviousUnread:
            break;
    }
    return "mark_previous_unread";
}

// "Ch. 3 · 2025-10-29 · Scanlator · Page 4 of 10": the second line of a row.
std::string chapterInfo(const core::Chapter& chapter) {
    std::vector<std::string> parts;
    const std::string number = chapterNumberText(chapter.chapterNumber);
    if (!number.empty()) {
        parts.push_back(brls::getStr("manga/chapter_number", number));
    }
    const std::string date = chapterDate(chapter.uploadDate);
    if (!date.empty()) {
        parts.push_back(date);
    }
    if (!chapter.scanlator.empty()) {
        parts.push_back(chapter.scanlator);
    }
    if (!chapter.isRead && chapter.lastPageRead > 0) {
        const int page = chapter.lastPageRead + 1;  // people count pages from 1
        parts.push_back(chapter.pageCount > 0 ? brls::getStr("manga/page_of", page, chapter.pageCount)
                                              : brls::getStr("manga/page", page));
    }
    std::string text;
    for (const std::string& part : parts) {
        text += (text.empty() ? "" : "  ·  ") + part;
    }
    return text;
}

// One chapter row: a bookmark stripe, the unread dot, the name and its details, and "..." (a tap opens its menu).
class ChapterCell : public GridCell {
  public:
    explicit ChapterCell(std::function<void(std::size_t)> onMenu) : onMenu_(std::move(onMenu)) {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));

        stripe_ = new brls::Box();
        stripe_->setWidth(6);
        stripe_->setHeight(kRowHeight);
        addView(stripe_);

        dot_ = new brls::Box();
        dot_->setWidth(10);
        dot_->setHeight(10);
        dot_->setCornerRadius(5);
        dot_->setMarginLeft(14);
        addView(dot_);

        auto* texts = new brls::Box(brls::Axis::COLUMN);
        texts->setWidth(kRowTextWidth);
        texts->setMarginLeft(14);
        name_ = new brls::Label();
        name_->setWidth(kRowTextWidth);
        name_->setFontSize(19);
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        texts->addView(name_);
        info_ = new brls::Label();
        info_->setWidth(kRowTextWidth);
        info_->setFontSize(15);
        info_->setMarginTop(6);
        info_->setSingleLine(true);
        info_->setAutoAnimate(false);
        info_->setTextColor(kSecondaryText);
        texts->addView(info_);
        addView(texts);

        // "..." drawn as three dots (no font needed). Its own tap wins over the row's (borealis asks the innermost view
        // first and cancels the others when it fires).
        auto* menu = new brls::Box(brls::Axis::ROW);
        menu->setWidth(kMenuWidth);
        menu->setHeight(kRowHeight);
        menu->setMarginLeft(8);
        menu->setJustifyContent(brls::JustifyContent::CENTER);
        menu->setAlignItems(brls::AlignItems::CENTER);
        for (int i = 0; i < 3; ++i) {
            auto* dot = new brls::Box();
            dot->setWidth(6);
            dot->setHeight(6);
            dot->setCornerRadius(3);
            dot->setMarginLeft(i == 0 ? 0 : 5);
            dot->setBackgroundColor(themeColor("brls/text_disabled"));
            menu->addView(dot);
        }
        menu->addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::END && onMenu_) {
                onMenu_(index());
            }
        }));
        addView(menu);
    }

    void show(const core::Chapter& chapter) {
        name_->setText(chapter.name.empty() ? brls::getStr("manga/chapter_number", chapterNumberText(chapter.chapterNumber))
                                            : chapter.name);
        name_->setTextColor(themeColor(chapter.isRead ? "brls/text_disabled" : "brls/text"));
        info_->setText(chapterInfo(chapter));
        stripe_->setBackgroundColor(chapter.isBookmarked ? themeColor("brls/accent") : nvgRGBA(0, 0, 0, 0));
        dot_->setBackgroundColor(chapter.isRead ? nvgRGBA(0, 0, 0, 0) : themeColor("brls/accent"));
    }

  private:
    std::function<void(std::size_t)> onMenu_;
    brls::Box* stripe_;
    brls::Box* dot_;
    brls::Label* name_;
    brls::Label* info_;
};

}  // namespace

MangaActivity::MangaActivity(app::AppContext& context, core::Manga manga,
                             std::function<void(const core::Manga&)> onChanged)
    : context_(context),
      model_(context.client, std::move(manga), static_cast<std::size_t>(std::max(1, context.profile.maxChapters))),
      onChanged_(std::move(onChanged)),
      reportedUnread_(model_.manga().unreadCount) {}

MangaActivity::~MangaActivity() {
    if (list_ != nullptr) {
        list_->setSource(nullptr);  // unbinds the rows while this screen still exists
    }
    model_.setListener({});
}

void MangaActivity::onContentAvailable() {
    if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
        frame->setTitle(model_.manga().title);
    }
    cover_ = new CoverImage(kCoverWidth, kCoverHeight);
    header->addView(cover_, 0);

    statusView_ = new StatusView();
    statusView_->setGrow(1.0f);
    body->addView(statusView_);
    list_ = new CoverGrid(kListMetrics);
    list_->setGrow(1.0f);
    list_->setVisibility(brls::Visibility::GONE);
    body->addView(list_);

    title->setLineHeight(kLineHeight);
    title->setIsWrapping(true);
    author->setTextColor(kSecondaryText);
    status->setTextColor(kSecondaryText);
    genres->setTextColor(kSecondaryText);
    genres->setIsWrapping(true);
    description->setLineHeight(kLineHeight);
    description->setIsWrapping(true);
    more->setTextColor(themeColor("brls/accent"));

    continueButton->registerClickAction([this](brls::View*) {
        continueReading();
        return true;
    });
    descriptionBox->registerClickAction([this](brls::View*) {
        toggleDescription();
        return true;
    });
    descriptionBox->addGestureRecognizer(new brls::TapGestureRecognizer(descriptionBox));
    sortButton->registerClickAction([this](brls::View*) {
        openSortMenu();
        return true;
    });
    filterButton->registerClickAction([this](brls::View*) {
        openFilterMenu();
        return true;
    });
    refreshButton->registerClickAction([this](brls::View*) {
        refresh();
        return true;
    });
    // On the list only: they act on the chapter with the focus.
    list_->registerAction("manga/toggle_read"_i18n, brls::BUTTON_X, [this](brls::View*) {
        const std::size_t index = list_->focusIndex();
        if (index < model_.items().size()) {
            act(model_.items()[index].id, model_.toggleReadAction(index));
        }
        return true;
    });
    list_->registerAction("manga/actions"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        openChapterMenu(list_->focusIndex());
        return true;
    });
    // PLUS quits the app everywhere, so the screen menu is on MINUS (D42).
    getContentView()->registerAction("manga/menu"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        openMenu();
        return true;
    });

    ready_ = true;
    model_.setListener([this] { modelChanged(); });
    list_->setSource(this);
    model_.start();
    modelChanged();

    reportScreen();
}

void MangaActivity::onResume() {
    reportScreen();
}

void MangaActivity::reportScreen() {
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["id"] = model_.manga().id;
    params["title"] = model_.manga().title;
    app::testhooks::screen("Manga", params);
}

void MangaActivity::modelChanged() {
    if (!ready_) {
        return;
    }
    showDetails();
    updateBar();
    updateContinue();

    switch (model_.state()) {
        case MangaModel::State::Loading:
            statusView_->showLoading("manga/loading"_i18n);
            showList(false);
            break;
        case MangaModel::State::Failed:
            statusView_->showError("Manga", model_.error(), [this] { model_.retry(); });
            showList(false);
            break;
        case MangaModel::State::Empty:
            statusView_->showMessage("manga/no_chapters"_i18n, "manga/no_chapters_text"_i18n);
            showList(false);
            break;
        case MangaModel::State::Ready: {
            const std::size_t count = model_.items().size();
            if (count == 0) {
                const bool unread = model_.filter() == ChapterFilter::Unread;
                statusView_->showMessage(unread ? "manga/none_unread"_i18n : "manga/none_bookmarked"_i18n,
                                         "manga/filter_empty_text"_i18n);
                showList(false);
                break;
            }
            const bool wasShown = listShown_;
            showList(true);
            if (!wasShown || model_.listRevision() != shownList_) {
                list_->reload(model_.rememberedFocus());
            } else {
                if (model_.itemsRevision() != shownItems_) {
                    list_->rebind();
                }
                if (count > shownCount_) {
                    list_->appended();
                }
            }
            break;
        }
    }
    shownList_ = model_.listRevision();
    shownItems_ = model_.itemsRevision();
    shownCount_ = listShown_ ? model_.items().size() : 0;

    if (model_.moreFailed() && !moreFailedShown_) {
        brls::Application::notify("manga/more_failed"_i18n);
    }
    moreFailedShown_ = model_.moreFailed();
    const std::optional<api::ApiError> failed = model_.takeActionError();
    if (failed) {
        brls::Application::notify(brls::getStr("manga/action_failed", errorText(*failed)));
    }
    if (!pendingAction_.empty() && !model_.busy()) {
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["action"] = pendingAction_;
        fields["chapter"] = pendingChapter_;
        fields["ok"] = !failed.has_value();
        app::testhooks::write("chapter_action", fields);
        pendingAction_.clear();
    }
    if (model_.manga().unreadCount != reportedUnread_ && model_.complete()) {
        reportedUnread_ = model_.manga().unreadCount;
        if (onChanged_) {
            onChanged_(model_.manga());
        }
    }
    report();
}

void MangaActivity::showDetails() {
    const core::Manga& manga = model_.manga();
    std::string genreText;
    for (const std::string& genre : manga.genre) {
        genreText += (genreText.empty() ? "" : ", ") + genre;
    }
    const std::string key = manga.title + '\n' + manga.author + '\n' + genreText + '\n' + manga.description + '\n' +
                            std::to_string(static_cast<int>(manga.status)) + '\n' + manga.thumbnailUrl + '\n' +
                            (model_.detailsLoaded() ? "1" : "0");
    if (key == detailsShown_) {
        return;
    }
    detailsShown_ = key;
    title->setText(fitLines(manga.title, kInfoWidth, title->getFont(), title->getFontSize(), kLineHeight, kTitleLines).text);
    cover_->load(manga.thumbnailUrl.empty() ? std::string() : context_.client.absoluteUrl(manga.thumbnailUrl), context_);
    if (!model_.detailsLoaded()) {
        // Only the library's copy so far: the rest follows with the details.
        author->setText("");
        status->setText("");
        genres->setText("");
        description->setText("");
        more->setVisibility(brls::Visibility::GONE);
        return;
    }
    author->setText(manga.author.empty() ? "manga/unknown_author"_i18n : brls::getStr("manga/author", manga.author));
    status->setText(statusText(manga.status));
    genres->setText(fitLines(genreText, kInfoWidth, genres->getFont(), genres->getFontSize(), kLineHeight, kGenreLines).text);
    showDescription();
}

void MangaActivity::showDescription() {
    const std::string& text = model_.manga().description;
    if (text.empty()) {
        description->setText("manga/no_description"_i18n);
        more->setVisibility(brls::Visibility::GONE);
        descriptionCut_ = false;
        return;
    }
    const FittedText fitted =
        fitLines(text, kDescriptionWidth, description->getFont(), description->getFontSize(), kLineHeight, kDescriptionLines);
    descriptionCut_ = fitted.cut;
    description->setText(expanded_ || !fitted.cut ? text : fitted.text);
    if (fitted.cut) {
        more->setText(expanded_ ? "manga/less"_i18n : "manga/more"_i18n);
        more->setVisibility(brls::Visibility::VISIBLE);
    } else {
        more->setVisibility(brls::Visibility::GONE);
    }
}

void MangaActivity::toggleDescription() {
    if (!descriptionCut_) {
        return;  // short: nothing to open
    }
    expanded_ = !expanded_;
    showDescription();
    report();
}

void MangaActivity::updateBar() {
    const ChapterSort sort = model_.sort();
    const ChapterFilter filter = model_.filter();
    sortButton->setText(brls::getStr("manga/sort_button", brls::getStr(kSortKeys[static_cast<int>(sort)])));
    filterButton->setText(brls::getStr("manga/filter_button", brls::getStr(kFilterKeys[static_cast<int>(filter)])));
    refreshButton->setText(model_.refreshing() ? "manga/refreshing"_i18n : "manga/refresh"_i18n);

    const int total = model_.totalCount();
    const auto loaded = static_cast<int>(model_.chapters().size());
    const auto shown = static_cast<int>(model_.items().size());
    std::string text;
    if (model_.state() == MangaModel::State::Ready || model_.state() == MangaModel::State::Empty) {
        if (!model_.complete()) {
            text = brls::getStr("manga/count_loading", loaded, total);
        } else if (filter != ChapterFilter::All) {
            text = brls::getStr("manga/count_loading", shown, loaded);
        } else {
            text = loaded == 1 ? "manga/count_one"_i18n : brls::getStr("manga/count", loaded);
        }
    }
    countLabel->setText(text);
}

void MangaActivity::updateContinue() {
    const bool known = model_.complete() || model_.moreFailed();
    const ContinuePoint point = model_.continuePoint();
    const core::Chapter* chapter = model_.chapter(point.chapterId);
    std::string text = "manga/continue"_i18n;
    bool enabled = false;
    if (known && point.kind == ContinuePoint::Kind::AllRead) {
        text = "manga/all_read"_i18n;
    } else if (known && chapter != nullptr) {
        enabled = true;
        if (point.kind == ContinuePoint::Kind::Start) {
            text = brls::getStr("manga/start_reading", chapter->name);
        } else if (point.page > 0) {
            text = brls::getStr("manga/continue_page", chapter->name, point.page + 1);
        } else {
            text = brls::getStr("manga/continue_reading", chapter->name);
        }
    }
    continueButton->setText(text);
    continueButton->setState(enabled ? brls::ButtonState::ENABLED : brls::ButtonState::DISABLED);
}

void MangaActivity::showList(bool visible) {
    if (visible == listShown_) {
        return;
    }
    brls::View* focus = brls::Application::getCurrentFocus();
    const bool hadFocus = inside(focus, list_) || inside(focus, statusView_);
    listShown_ = visible;
    if (visible) {
        statusView_->setVisibility(brls::Visibility::GONE);
        list_->setVisibility(brls::Visibility::VISIBLE);
        if (hadFocus) {
            brls::Application::giveFocus(list_);
        }
    } else {
        list_->setVisibility(brls::Visibility::GONE);
        statusView_->setVisibility(brls::Visibility::VISIBLE);
        // First switch the visibility, then move the focus: a hidden view refuses it (D42).
        if (hadFocus) {
            brls::Application::giveFocus(statusView_);
        }
    }
}

std::size_t MangaActivity::count() const {
    return model_.state() == MangaModel::State::Ready ? model_.items().size() : 0;
}

GridCell* MangaActivity::makeCell() {
    return new ChapterCell([this](std::size_t index) { openChapterMenu(index); });
}

void MangaActivity::bind(GridCell& cell, std::size_t index) {
    cell.setId("manga/chapter-" + std::to_string(index));  // the "focus" test event says which row has the focus
    static_cast<ChapterCell&>(cell).show(model_.items()[index]);
}

void MangaActivity::unbind(GridCell& cell) {
    (void)cell;  // rows hold no images or loads
}

void MangaActivity::select(std::size_t index) {
    if (index >= model_.items().size()) {
        return;
    }
    model_.rememberFocus(index);
    const core::Chapter& chapter = model_.items()[index];
    openChapter(chapter, chapter.isRead ? 0 : std::max(0, chapter.lastPageRead));
}

void MangaActivity::focused(std::size_t index) {
    model_.rememberFocus(index);
}

void MangaActivity::showing(std::size_t first, std::size_t last) {
    // For the tests: which rows have cells now.
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["first"] = first;
    fields["last"] = last;
    app::testhooks::write("chapter_list", fields);
}

void MangaActivity::continueReading() {
    if (!model_.complete() && !model_.moreFailed()) {
        return;  // not final yet
    }
    const ContinuePoint point = model_.continuePoint();
    const core::Chapter* chapter = model_.chapter(point.chapterId);
    if (chapter != nullptr &&
        (point.kind == ContinuePoint::Kind::Start || point.kind == ContinuePoint::Kind::Continue)) {
        openChapter(*chapter, point.page);
    }
}

void MangaActivity::openChapter(const core::Chapter& chapter, int page) {
    brls::Application::pushActivity(new ReaderActivity(context_, model_.manga(), chapter, page));
}

void MangaActivity::openChapterMenu(std::size_t index) {
    const std::vector<MangaModel::Action> actions = model_.actionsFor(index);
    if (actions.empty()) {
        return;
    }
    const core::Chapter& chapter = model_.items()[index];
    model_.rememberFocus(index);
    std::vector<std::string> names;
    for (const MangaModel::Action action : actions) {
        names.push_back(brls::getStr(actionKey(action)));
    }
    const int chapterId = chapter.id;
    auto* dropdown = new brls::Dropdown(chapter.name, names, [this, actions, chapterId](int selected) {
        if (selected < 0 || static_cast<std::size_t>(selected) >= actions.size()) {
            return;
        }
        const MangaModel::Action action = actions[static_cast<std::size_t>(selected)];
        brls::sync([this, chapterId, action] { act(chapterId, action); });  // after the menu has closed
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaActivity::act(int chapterId, MangaModel::Action action) {
    // The list may have changed while the menu was open: find the chapter again.
    const auto& items = model_.items();
    const auto found =
        std::find_if(items.begin(), items.end(), [chapterId](const core::Chapter& c) { return c.id == chapterId; });
    if (found == items.end() || model_.busy()) {
        return;
    }
    pendingAction_ = actionName(action);
    pendingChapter_ = chapterId;
    if (!model_.act(static_cast<std::size_t>(found - items.begin()), action)) {
        pendingAction_.clear();
        brls::Application::notify("manga/nothing_to_change"_i18n);
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["action"] = actionName(action);
        fields["chapter"] = chapterId;
        fields["ok"] = true;
        fields["changed"] = false;
        app::testhooks::write("chapter_action", fields);
    }
}

void MangaActivity::refresh() {
    if (model_.state() == MangaModel::State::Failed) {
        model_.retry();
        return;
    }
    if (model_.moreFailed() && !model_.busy()) {
        model_.retry();  // carries on loading where the list stopped
        return;
    }
    if (model_.busy()) {
        return;
    }
    pendingAction_ = "refresh";
    pendingChapter_ = 0;
    model_.refreshFromSource();
    if (!model_.busy()) {
        pendingAction_.clear();  // it couldn't start (still loading)
    }
}

void MangaActivity::openSortMenu() {
    std::vector<std::string> names;
    for (const char* key : kSortKeys) {
        names.push_back(brls::getStr(key));
    }
    auto* dropdown = new brls::Dropdown(
        "manga/sort"_i18n, names,
        [this](int selected) {
            if (selected >= 0 && selected < 2) {
                model_.setSort(kSorts[selected]);
            }
        },
        static_cast<int>(model_.sort()));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaActivity::openFilterMenu() {
    std::vector<std::string> names;
    for (const char* key : kFilterKeys) {
        names.push_back(brls::getStr(key));
    }
    auto* dropdown = new brls::Dropdown(
        "manga/filter"_i18n, names,
        [this](int selected) {
            if (selected >= 0 && selected < 3) {
                model_.setFilter(kFilters[selected]);
            }
        },
        static_cast<int>(model_.filter()));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaActivity::openMenu() {
    const std::vector<std::string> entries = {"manga/menu_sort"_i18n, "manga/menu_filter"_i18n,
                                              "manga/menu_refresh"_i18n};
    auto* dropdown = new brls::Dropdown("manga/menu_title"_i18n, entries, [this](int selected) {
        // The menu closes first; the next one opens on the following frame.
        brls::sync([this, selected] {
            if (selected == 0) {
                openSortMenu();
            } else if (selected == 1) {
                openFilterMenu();
            } else if (selected == 2) {
                refresh();
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void MangaActivity::report() {
    if (model_.state() == MangaModel::State::Loading || model_.busy()) {
        reported_.clear();  // after a reload the same result is reported again
        return;
    }
    static const char* const kStates[] = {"loading", "ready", "empty", "failed"};
    static const char* const kKinds[] = {"none", "start", "continue", "all_read"};
    const ContinuePoint point = model_.continuePoint();
    nlohmann::ordered_json next = nlohmann::ordered_json::object();
    next["kind"] = kKinds[static_cast<int>(point.kind)];
    next["chapter"] = point.chapterId;
    next["page"] = point.page;
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = kStates[static_cast<int>(model_.state())];
    fields["id"] = model_.manga().id;
    fields["items"] = model_.items().size();
    fields["loaded"] = model_.chapters().size();
    fields["total"] = model_.totalCount();
    fields["complete"] = model_.complete();
    fields["sort"] = std::string(chapterSortName(model_.sort()));
    fields["filter"] = std::string(chapterFilterName(model_.filter()));
    fields["first"] = model_.items().empty() ? 0 : model_.items().front().sourceOrder;
    fields["unread"] = model_.manga().unreadCount;
    fields["continue"] = next;
    fields["description"] = !descriptionCut_ ? "short" : (expanded_ ? "open" : "closed");
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        app::testhooks::write("manga", fields);
    }
}

}  // namespace snx::ui
