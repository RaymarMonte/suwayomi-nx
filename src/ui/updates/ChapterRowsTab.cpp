#include "ui/updates/ChapterRowsTab.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <utility>

#include "app/TestHooks.hpp"
#include "platform/Platform.hpp"
#include "ui/components/CoverImage.hpp"
#include "ui/manga/MangaActivity.hpp"
#include "ui/reader/ReaderActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of rows: the tab is 1280 - 410 px (sidebar) wide; rows are far over the 72 px touch minimum (D16).
constexpr float kRowWidth = 800;
constexpr float kRowHeight = 96;
constexpr GridMetrics kMetrics{kRowWidth, kRowHeight, 8, 12};
constexpr float kCoverWidth = 60;
constexpr float kCoverHeight = 90;
constexpr float kDayWidth = 130;
constexpr float kMenuWidth = 72;  // the "..." at the end of an Updates row

NVGcolor themeColor(const char* name) {
    return brls::Application::getTheme()[name];
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

// The time a row is sorted by: when the chapter was found (Updates) or read (History).
std::int64_t rowTime(const core::ChapterEntry& entry, bool updates) {
    return updates ? entry.chapter.fetchedAt : entry.chapter.lastReadAt;
}

// "Chapter 3", or the number when the server has no name.
std::string chapterTitle(const core::Chapter& chapter) {
    return chapter.name.empty() ? brls::getStr("manga/chapter_number", chapterNumberText(chapter.chapterNumber))
                                : chapter.name;
}

// The second line of a row. Updates: the chapter, "· Page 5" once started. History: "Chapter 5 · page 12" or "· read".
std::string rowInfo(const core::ChapterEntry& entry, bool updates) {
    const core::Chapter& chapter = entry.chapter;
    if (updates) {
        if (!chapter.isRead && chapter.lastPageRead > 0) {
            return chapterTitle(chapter) + " · " + brls::getStr("updates/page", chapter.lastPageRead + 1);
        }
        return chapterTitle(chapter);
    }
    if (chapter.isRead) {
        return brls::getStr("history/chapter_read", chapterTitle(chapter));
    }
    return brls::getStr("history/chapter_page", chapterTitle(chapter), chapter.lastPageRead + 1);
}

// History's note for a save of this manga that waits in the queue (WP-15b): "page 5 not saved yet", with the chapter's
// name in front when it isn't the row's chapter (read while the server was away, so the server's history doesn't show it).
std::string waitingNote(const core::WaitingSave& waiting, int rowChapterId) {
    const std::string what =
        waiting.read ? "history/waiting_read"_i18n : brls::getStr("history/waiting_page", waiting.page + 1);
    if (waiting.chapterId == rowChapterId || waiting.chapterName.empty()) {
        return what;
    }
    return brls::getStr("history/waiting_other", waiting.chapterName, what);
}

// One row: the cover (a tap opens the details screen), the manga's title and the chapter line, the day on the first
// row of a day, and on Updates a "..." for the row menu (its own tap wins over the row's: borealis asks the innermost
// view first).
class RowCell : public GridCell {
  public:
    RowCell(bool withMenu, std::function<void(std::size_t)> onCover, std::function<void(std::size_t)> onMenu)
        : onCover_(std::move(onCover)), onMenu_(std::move(onMenu)) {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));

        cover_ = new CoverImage(kCoverWidth, kCoverHeight);
        cover_->setMarginLeft(8);
        cover_->addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::END && onCover_) {
                onCover_(index());
            }
        }));
        addView(cover_);

        const float textWidth = kRowWidth - 8 - kCoverWidth - 16 - kDayWidth - (withMenu ? kMenuWidth : 0) - 16;
        auto* texts = new brls::Box(brls::Axis::COLUMN);
        texts->setWidth(textWidth);
        texts->setMarginLeft(16);
        title_ = new brls::Label();
        title_->setWidth(textWidth);
        title_->setFontSize(19);
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        texts->addView(title_);
        info_ = new brls::Label();
        info_->setWidth(textWidth);
        info_->setFontSize(15);
        info_->setMarginTop(6);
        info_->setSingleLine(true);
        info_->setAutoAnimate(false);
        texts->addView(info_);
        addView(texts);

        day_ = new brls::Label();
        day_->setWidth(kDayWidth);
        day_->setFontSize(15);
        day_->setSingleLine(true);
        day_->setAutoAnimate(false);
        day_->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        day_->setTextColor(themeColor("brls/accent"));
        addView(day_);

        if (withMenu) {
            auto* menu = new brls::Box(brls::Axis::ROW);
            menu->setWidth(kMenuWidth);
            menu->setHeight(kRowHeight);
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
    }

    // note: History's waiting note, or "".
    void show(const core::ChapterEntry& entry, bool updates, const std::string& day, bool opening,
              const std::string& note, app::AppContext& context) {
        title_->setText(entry.manga.title);
        title_->setTextColor(themeColor(entry.chapter.isRead && updates ? "brls/text_disabled" : "brls/text"));
        if (opening) {
            info_->setText("updates/opening"_i18n);
        } else if (!note.empty()) {
            info_->setText(rowInfo(entry, updates) + " · " + note);
        } else {
            info_->setText(rowInfo(entry, updates));
        }
        info_->setTextColor(themeColor("brls/text_disabled"));
        day_->setText(day);
        const std::string url =
            entry.manga.thumbnailUrl.empty() ? std::string() : context.client.absoluteUrl(entry.manga.thumbnailUrl);
        if (url != coverUrl_) {
            coverUrl_ = url;
            cover_->load(url, context);
        }
    }

    void clear() {
        cover_->clear();
        coverUrl_.clear();
    }

  private:
    std::function<void(std::size_t)> onCover_;
    std::function<void(std::size_t)> onMenu_;
    CoverImage* cover_;
    brls::Label* title_;
    brls::Label* info_;
    brls::Label* day_;
    std::string coverUrl_;
};

}  // namespace

ChapterRowsTab::ChapterRowsTab(app::AppContext& context, std::shared_ptr<ChapterRowsModel> rows,
                               std::shared_ptr<LibraryUpdater> updater, std::function<void()> onRead,
                               std::function<void(const core::Manga&)> onMangaChanged)
    : context_(context),
      rowsOwner_(std::move(rows)),
      updaterOwner_(std::move(updater)),
      rows_(*rowsOwner_),
      updater_(updaterOwner_.get()),
      onRead_(std::move(onRead)),
      onMangaChanged_(std::move(onMangaChanged)) {
    setAxis(brls::Axis::COLUMN);
    setWidth(brls::View::AUTO);
    setHeight(brls::View::AUTO);
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    offsetSeconds_ = platform::utcOffsetSeconds(now);
    today_ = core::dayKey(now, offsetSeconds_);

    if (isUpdates()) {
        buildHeader();
    }
    status_ = new StatusView();
    status_->setGrow(1.0f);
    addView(status_);
    grid_ = new CoverGrid(kMetrics);
    grid_->setGrow(1.0f);
    grid_->setVisibility(brls::Visibility::GONE);
    addView(grid_);
    setDefaultFocusedIndex(isUpdates() ? 2 : 1);  // from the tab list, the focus lands on the rows

    if (isUpdates()) {
        registerAction("updates/toggle_read"_i18n, brls::BUTTON_X, [this](brls::View*) {
            if (gridShown_) {
                rows_.toggleRead(grid_->focusIndex());
            }
            return true;
        });
        registerAction("updates/row_menu"_i18n, brls::BUTTON_Y, [this](brls::View*) {
            if (gridShown_) {
                openRowMenu(grid_->focusIndex());
            }
            return true;
        });
        registerAction("updates/menu"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
            openMenu();
            return true;
        });
    } else {
        registerAction("history/refresh"_i18n, brls::BUTTON_X, [this](brls::View*) {
            rows_.reload();
            return true;
        });
        registerAction("history/details"_i18n, brls::BUTTON_Y, [this](brls::View*) {
            if (gridShown_) {
                openManga(grid_->focusIndex());
            }
            return true;
        });
        registerAction("history/menu"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
            openMenu();
            return true;
        });
        // A save of a manga in the list went into the queue or left it: the rows show or drop their note.
        savesListener_ = context_.saves.addListener([this](WaitingSaves::Change, const core::WaitingSave&) {
            if (gridShown_) {
                grid_->rebind();
            }
            report();
        });
    }
    // B cancels "Opening..." (both tabs). Only while an open is under way; otherwise B does what it does elsewhere.
    registerAction("updates/cancel_open"_i18n, brls::BUTTON_B, [this](brls::View*) {
        if (opener_ == nullptr) {
            return false;
        }
        brls::Logger::info("{}: opening cancelled", isUpdates() ? "updates" : "history");
        stopOpening();
        return true;
    });
    setActionAvailable(brls::BUTTON_B, false);

    rows_.setListener([this] { modelChanged(); });
    rows_.setMangaChanged(onMangaChanged_);
    grid_->setSource(this);
    if (updater_ != nullptr) {
        updaterListener_ = updater_->addListener([this] { updaterChanged(); });
        updater_->check();  // "Last updated", and an update already running elsewhere
        updaterChanged();
    }
    rows_.start();  // only the first time
    modelChanged();
}

ChapterRowsTab::~ChapterRowsTab() {
    *alive_ = false;
    context_.saves.removeListener(savesListener_);  // 0 (Updates): nothing to remove
    opener_.reset();
    grid_->setSource(nullptr);  // unbinds the cells (cancels their cover loads) while this tab still exists
    rows_.setListener({});
    rows_.setMangaChanged({});
    if (updater_ != nullptr) {
        updater_->removeListener(updaterListener_);
    }
}

void ChapterRowsTab::buildHeader() {
    header_ = new brls::Box(brls::Axis::ROW);
    header_->setHeight(96);
    header_->setAlignItems(brls::AlignItems::CENTER);
    header_->setPadding(12, 24, 12, 24);
    updateButton_ = new brls::Button();
    updateButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    updateButton_->setHeight(72);
    updateButton_->setWidth(220);
    updateButton_->setText("updates/update_library"_i18n);
    updateButton_->registerClickAction([this](brls::View*) {
        if (updater_->active()) {
            updater_->stop();
        } else {
            updater_->start();
        }
        return true;
    });
    header_->addView(updateButton_);

    auto* column = new brls::Box(brls::Axis::COLUMN);
    column->setMarginLeft(20);
    column->setGrow(1.0f);
    statusLine_ = new brls::Label();
    statusLine_->setFontSize(16);
    statusLine_->setSingleLine(true);
    statusLine_->setAutoAnimate(false);
    statusLine_->setTextColor(themeColor("brls/text_disabled"));
    column->addView(statusLine_);
    barTrack_ = new brls::Box(brls::Axis::ROW);
    barTrack_->setHeight(4);
    barTrack_->setMarginTop(8);
    barTrack_->setWidth(420);
    barTrack_->setCornerRadius(2);
    barTrack_->setBackgroundColor(nvgRGBA(128, 128, 128, 60));
    barFill_ = new brls::Box();
    barFill_->setHeight(4);
    barFill_->setWidth(0);
    barFill_->setCornerRadius(2);
    barFill_->setBackgroundColor(themeColor("brls/accent"));
    barTrack_->addView(barFill_);
    barTrack_->setVisibility(brls::Visibility::INVISIBLE);
    column->addView(barTrack_);
    header_->addView(column);
    addView(header_);
}

void ChapterRowsTab::updaterChanged() {
    if (updater_ == nullptr) {
        return;
    }
    if (auto error = updater_->takeError()) {
        brls::Application::notify(errorText(*error));
    }
    const LibraryUpdater& u = *updater_;
    updateButton_->setText(u.active() ? "updates/stop"_i18n : "updates/update_library"_i18n);
    std::string line;
    float share = -1;
    const auto& status = u.lastStatus();
    if (u.pollFailed()) {
        line = "updates/cant_reach"_i18n;
    } else if (u.stopping()) {
        line = "updates/stopping"_i18n;
    } else if (u.phase() == LibraryUpdater::Phase::Starting) {
        line = "updates/starting"_i18n;
        share = 0;
    } else if (u.phase() == LibraryUpdater::Phase::Running && status) {
        line = brls::getStr("updates/updating", status->finished, status->total);
        share = status->total > 0 ? static_cast<float>(status->finished) / static_cast<float>(status->total) : 0.0f;
    } else if (u.phase() == LibraryUpdater::Phase::Finished && u.summary()) {
        const LibraryUpdater::Summary& s = *u.summary();
        if (s.nothingToUpdate) {
            line = "updates/nothing"_i18n;
        } else {
            line = s.newChapters == 1   ? "updates/summary_new_one"_i18n
                   : s.newChapters == 0 ? "updates/summary_none"_i18n
                                        : brls::getStr("updates/summary_new", s.newChapters);
            if (s.skippedManga > 0) {
                line += " · " + brls::getStr("updates/summary_skipped", s.skippedManga);
            }
            if (!s.failed.empty()) {
                line += " · " + brls::getStr("updates/summary_failed", s.failed.size());
            }
            if (s.stopped) {
                line += " · " + "updates/summary_stopped"_i18n;
            }
        }
    } else if (status && status->lastUpdateMs > 0) {
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        line = brls::getStr("updates/last_updated",
                            core::formatDateTime(status->lastUpdateMs / 1000, offsetSeconds_, now));
    } else if (status) {
        line = "updates/never_updated"_i18n;
    }
    statusLine_->setText(line);
    if (share >= 0) {
        barTrack_->setVisibility(brls::Visibility::VISIBLE);
        barFill_->setWidth(420 * std::clamp(share, 0.0f, 1.0f));
    } else {
        barTrack_->setVisibility(brls::Visibility::INVISIBLE);
    }
}

void ChapterRowsTab::showGrid(bool visible) {
    if (visible == gridShown_) {
        return;
    }
    brls::View* focus = brls::Application::getCurrentFocus();
    const bool hadFocus = inside(focus, grid_) || inside(focus, status_);
    gridShown_ = visible;
    if (visible) {
        status_->setVisibility(brls::Visibility::GONE);
        grid_->setVisibility(brls::Visibility::VISIBLE);
        if (hadFocus) {
            brls::Application::giveFocus(grid_);
        }
    } else {
        grid_->setVisibility(brls::Visibility::GONE);
        status_->setVisibility(brls::Visibility::VISIBLE);
        // First visible, then the focus (a hidden view refuses it, D42).
        if (hadFocus) {
            brls::Application::giveFocus(status_);
        }
    }
}

void ChapterRowsTab::modelChanged() {
    if (auto error = rows_.takeError()) {
        brls::Application::notify(errorText(*error));
    }
    const bool updates = isUpdates();
    std::vector<std::int64_t> times;
    times.reserve(rows_.items().size());
    for (const core::ChapterEntry& entry : rows_.items()) {
        times.push_back(rowTime(entry, updates));
    }
    groups_ = core::dayGroups(times, offsetSeconds_);

    switch (rows_.state()) {
        case ChapterRowsModel::State::Loading:
            status_->showLoading(updates ? "updates/loading"_i18n : "history/loading"_i18n);
            showGrid(false);
            break;
        case ChapterRowsModel::State::Failed:
            status_->showError(updates ? "Updates" : "History", rows_.error(), [this] { rows_.reload(); });
            showGrid(false);
            break;
        case ChapterRowsModel::State::Empty:
            if (updates) {
                status_->showMessage("updates/empty_title"_i18n, "updates/empty_text"_i18n);
            } else {
                status_->showMessage("history/empty_title"_i18n, "history/empty_text"_i18n);
            }
            showGrid(false);
            break;
        case ChapterRowsModel::State::Ready: {
            const std::size_t count = rows_.items().size();
            showGrid(true);
            if (shownState_ != ChapterRowsModel::State::Ready || rows_.listRevision() != shownList_ ||
                count < shownCount_) {
                grid_->reload(std::min(rows_.rememberedFocus(), count - 1));
            } else if (count > shownCount_) {
                grid_->appended();
            } else if (rows_.itemsRevision() != shownItems_) {
                grid_->rebind();
            }
            shownCount_ = count;
            break;
        }
    }
    if (rows_.moreFailed()) {
        brls::Application::notify("updates/more_failed"_i18n);
    }
    shownState_ = rows_.state();
    shownList_ = rows_.listRevision();
    shownItems_ = rows_.itemsRevision();
    if (shownState_ != ChapterRowsModel::State::Ready) {
        shownCount_ = 0;
    }
    report();
}

void ChapterRowsTab::report() {
    if (rows_.state() == ChapterRowsModel::State::Loading) {
        reported_.clear();  // after a reload the same result is reported again
        return;
    }
    static const char* const kStates[] = {"loading", "ready", "empty", "failed"};
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = kStates[static_cast<int>(rows_.state())];
    fields["rows"] = rows_.items().size();
    fields["groups"] = groups_.size();
    nlohmann::ordered_json first = nlohmann::ordered_json::array();
    for (std::size_t i = 0; i < rows_.items().size() && i < 3; ++i) {
        const core::ChapterEntry& entry = rows_.items()[i];
        nlohmann::ordered_json row = nlohmann::ordered_json::object();
        row["chapterId"] = entry.chapter.id;
        row["mangaId"] = entry.manga.id;
        row["title"] = entry.manga.title;
        row["chapter"] = entry.chapter.name;
        row["page"] = entry.chapter.lastPageRead;
        row["read"] = entry.chapter.isRead;
        row["day"] = dayText(i);
        if (const core::WaitingSave* waiting = waitingOf(i)) {
            row["waitingChapterId"] = waiting->chapterId;  // WP-15b: the row shows "page N not saved yet"
            row["waitingPage"] = waiting->page;
        }
        first.push_back(row);
    }
    fields["first"] = first;
    if (!isUpdates()) {
        std::size_t waiting = 0;  // rows with a waiting note
        for (std::size_t i = 0; i < rows_.items().size(); ++i) {
            waiting += waitingOf(i) != nullptr ? 1 : 0;
        }
        fields["waiting"] = waiting;
    }
    if (isUpdates()) {
        fields["newest"] = rows_.newestFetchedAt();
    }
    fields["hidden"] = rows_.hidden();
    fields["pages"] = rows_.pages();
    fields["end"] = rows_.ended();
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        app::testhooks::write(isUpdates() ? "updates" : "history", fields);
    }
}

std::string ChapterRowsTab::dayText(std::size_t index) const {
    for (const core::DayGroup& group : groups_) {
        if (group.first == index) {
            const core::DayLabel label = core::dayLabel(group.day, today_);
            switch (label.kind) {
                case core::DayLabel::Kind::Today:
                    return "updates/today"_i18n;
                case core::DayLabel::Kind::Yesterday:
                    return "updates/yesterday"_i18n;
                case core::DayLabel::Kind::Date:
                    return core::formatDate(label);
            }
        }
        if (group.first > index) {
            break;
        }
    }
    return {};
}

std::size_t ChapterRowsTab::count() const {
    return rows_.state() == ChapterRowsModel::State::Ready ? rows_.items().size() : 0;
}

GridCell* ChapterRowsTab::makeCell() {
    return new RowCell(isUpdates(), [this](std::size_t index) { openManga(index); },
                       [this](std::size_t index) { openRowMenu(index); });
}

void ChapterRowsTab::bind(GridCell& cell, std::size_t index) {
    cell.setId((isUpdates() ? "updates/row-" : "history/row-") + std::to_string(index));  // the "focus" test event
    const bool opening = opener_ != nullptr && openingIndex_ == index;
    const core::WaitingSave* waiting = waitingOf(index);
    const std::string note = waiting != nullptr ? waitingNote(*waiting, rows_.items()[index].chapter.id) : std::string();
    static_cast<RowCell&>(cell).show(rows_.items()[index], isUpdates(), dayText(index), opening, note, context_);
}

const core::WaitingSave* ChapterRowsTab::waitingOf(std::size_t index) const {
    if (isUpdates() || index >= rows_.items().size()) {
        return nullptr;  // Updates shows the server's chapters only
    }
    return context_.saves.waitingForManga(rows_.items()[index].manga.id);
}

void ChapterRowsTab::unbind(GridCell& cell) {
    static_cast<RowCell&>(cell).clear();
}

void ChapterRowsTab::select(std::size_t index) {
    openRow(index);
}

void ChapterRowsTab::focused(std::size_t index) {
    rows_.rememberFocus(index);
}

void ChapterRowsTab::showing(std::size_t first, std::size_t last) {
    (void)first;
    if (last > 0) {
        rows_.showing(last - 1);
    }
}

void ChapterRowsTab::openRow(std::size_t index) {
    if (index >= rows_.items().size()) {
        return;
    }
    rows_.rememberFocus(index);
    const core::ChapterEntry& entry = rows_.items()[index];
    // A new open replaces one under way (its loads are cancelled with it).
    opener_ = std::make_unique<MangaModel>(context_.client, entry.manga, context_.profile.maxChapters);
    openingIndex_ = index;
    openingChapterId_ = isUpdates() ? entry.chapter.id : 0;
    openStartMs_ = nowMs();
    ++openTicket_;
    opener_->setListener([this] { openerChanged(); });
    opener_->start();
    setActionAvailable(brls::BUTTON_B, true);  // B cancels
    grid_->rebind();  // "Opening..."
}

void ChapterRowsTab::openerChanged() {
    if (opener_ == nullptr) {
        return;
    }
    MangaModel& model = *opener_;
    if (model.state() == MangaModel::State::Failed) {
        brls::Application::notify(brls::getStr("updates/open_failed", errorText(model.error())));
        brls::sync([this, alive = std::weak_ptr<bool>(alive_), ticket = openTicket_] {
            if (!alive.expired() && *alive.lock() && ticket == openTicket_) {
                stopOpening();
            }
        });
        return;
    }
    if (!model.detailsLoaded() || !(model.complete() || model.moreFailed())) {
        return;  // the whole list first (next / previous chapter, D44)
    }
    int chapterId = openingChapterId_;
    int page = 0;
    if (chapterId != 0) {
        const core::Chapter* chapter = model.chapter(chapterId);
        if (chapter != nullptr) {
            page = chapter->isRead ? 0 : std::max(0, chapter->lastPageRead);  // a read chapter opens at its start (D44)
        }
    } else {
        const ContinuePoint point = model.continuePoint();
        if (point.kind == ContinuePoint::Kind::Start || point.kind == ContinuePoint::Kind::Continue) {
            chapterId = point.chapterId;
            page = point.page;
        }
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["from"] = isUpdates() ? "updates" : "history";
    fields["chapterId"] = chapterId;
    fields["page"] = page;
    fields["chapters"] = model.chapters().size();
    fields["ms"] = nowMs() - openStartMs_;
    app::testhooks::write("chapter_open", fields);
    const core::Manga manga = model.manga();
    const std::vector<core::Chapter> chapters = model.chapters();
    // Not inside the model's own callback: the model goes on the next frame.
    brls::sync([this, alive = std::weak_ptr<bool>(alive_), ticket = openTicket_, manga, chapters, chapterId, page] {
        if (alive.expired() || !*alive.lock() || ticket != openTicket_) {
            return;  // this view went, or B cancelled the open (or another row replaced it) meanwhile
        }
        stopOpening();
        if (chapterId == 0 || chapters.empty()) {
            // Every chapter read (or none): the details screen instead.
            brls::Application::pushActivity(new MangaActivity(context_, manga, onMangaChanged_));
            return;
        }
        auto onClosed = [alive, onRead = onRead_] {
            (void)alive;
            if (onRead) {
                onRead();  // MainActivity's: it outlives this view
            }
        };
        brls::Application::pushActivity(new ReaderActivity(context_, manga, chapters, chapterId, page, onClosed));
    });
}

void ChapterRowsTab::stopOpening() {
    opener_.reset();  // cancels its loads
    ++openTicket_;    // and a step it posted already does nothing
    setActionAvailable(brls::BUTTON_B, false);
    grid_->rebind();
}

void ChapterRowsTab::openManga(std::size_t index) {
    if (index >= rows_.items().size()) {
        return;
    }
    rows_.rememberFocus(index);
    brls::Application::pushActivity(new MangaActivity(context_, rows_.items()[index].manga, onMangaChanged_));
}

void ChapterRowsTab::openRowMenu(std::size_t index) {
    if (!isUpdates() || index >= rows_.items().size()) {
        return;
    }
    rows_.rememberFocus(index);
    const core::ChapterEntry& entry = rows_.items()[index];
    const bool read = entry.chapter.isRead;
    const std::vector<std::string> entries = {"updates/open_manga"_i18n,
                                              read ? "updates/mark_unread"_i18n : "updates/mark_read"_i18n};
    auto* dropdown = new brls::Dropdown(entry.manga.title, entries, [this, index, read](int selected) {
        brls::sync([this, index, read, selected] {
            if (selected == 0) {
                openManga(index);
            } else if (selected == 1) {
                rows_.setRead(index, !read);
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ChapterRowsTab::openMenu() {
    std::vector<std::string> entries;
    if (isUpdates()) {
        entries = {updater_->active() ? "updates/menu_stop"_i18n : "updates/menu_update"_i18n,
                   "updates/menu_refresh"_i18n};
    } else {
        entries = {"history/menu_refresh"_i18n};
    }
    auto* dropdown = new brls::Dropdown(isUpdates() ? "updates/menu_title"_i18n : "history/menu_title"_i18n, entries,
                                        [this](int selected) {
                                            // The menu closes first; the action runs on the following frame.
                                            brls::sync([this, selected] {
                                                if (isUpdates() && selected == 0) {
                                                    if (updater_->active()) {
                                                        updater_->stop();
                                                    } else {
                                                        updater_->start();
                                                    }
                                                } else if (selected >= 0) {
                                                    rows_.reload();
                                                }
                                            });
                                        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace snx::ui
