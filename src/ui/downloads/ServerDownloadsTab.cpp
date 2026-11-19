#include "ui/downloads/ServerDownloadsTab.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "app/TestHooks.hpp"
#include "ui/components/CoverImage.hpp"
#include "ui/manga/MangaActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of rows, the size of the Updates rows (WP-15): far over the 72 px touch minimum (D16).
constexpr float kRowWidth = 800;
constexpr float kRowHeight = 96;
constexpr GridMetrics kMetrics{kRowWidth, kRowHeight, 8, 12};
constexpr float kCoverWidth = 60;
constexpr float kCoverHeight = 90;
constexpr float kStateWidth = 220;
constexpr float kBarWidth = 200;
// Secondary text (a row's chapter, the header's hint): the details screen's grey, lighter than the theme's "disabled" one.
const NVGcolor kSecondaryText = nvgRGB(150, 150, 150);

NVGcolor themeColor(const char* name) {
    return brls::Application::getTheme()[name];
}

double steadyMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

// "Queued", "Downloading 40 %", "Paused at 66 %", "Failed after 3 tries".
std::string stateText(const core::DownloadRow& row) {
    switch (row.state) {
        case core::DownloadState::Downloading:
            return brls::getStr("server_downloads/state_downloading", core::percentText(row.progress));
        case core::DownloadState::Error:
            return brls::getStr("server_downloads/state_failed", row.tries);
        case core::DownloadState::Queued:
            break;
    }
    if (row.progress > 0) {  // stopped in the middle: it goes on with the pages left
        return brls::getStr("server_downloads/state_paused", core::percentText(row.progress));
    }
    return "server_downloads/state_queued"_i18n;
}

const char* stateName(core::DownloadState state) {
    switch (state) {
        case core::DownloadState::Queued: return "queued";
        case core::DownloadState::Downloading: return "downloading";
        case core::DownloadState::Error: return "failed";
    }
    return "queued";
}

// One row: the cover, the manga's title and the chapter, the state and a thin progress bar on the right.
class DownloadCell : public GridCell {
  public:
    DownloadCell() {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));

        cover_ = new CoverImage(kCoverWidth, kCoverHeight);
        cover_->setMarginLeft(8);
        addView(cover_);

        const float textWidth = kRowWidth - 8 - kCoverWidth - 16 - kStateWidth - 24;
        auto* texts = new brls::Box(brls::Axis::COLUMN);
        texts->setWidth(textWidth);
        texts->setMarginLeft(16);
        title_ = new brls::Label();
        title_->setWidth(textWidth);
        title_->setFontSize(19);
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        texts->addView(title_);
        chapter_ = new brls::Label();
        chapter_->setWidth(textWidth);
        chapter_->setFontSize(15);
        chapter_->setMarginTop(6);
        chapter_->setSingleLine(true);
        chapter_->setAutoAnimate(false);
        chapter_->setTextColor(kSecondaryText);
        texts->addView(chapter_);
        addView(texts);

        auto* right = new brls::Box(brls::Axis::COLUMN);
        right->setWidth(kStateWidth);
        right->setAlignItems(brls::AlignItems::FLEX_END);
        state_ = new brls::Label();
        state_->setWidth(kStateWidth);
        state_->setFontSize(15);
        state_->setSingleLine(true);
        state_->setAutoAnimate(false);
        state_->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        right->addView(state_);
        track_ = new brls::Box(brls::Axis::ROW);
        track_->setWidth(kBarWidth);
        track_->setHeight(4);
        track_->setMarginTop(10);
        track_->setCornerRadius(2);
        track_->setBackgroundColor(nvgRGBA(128, 128, 128, 60));
        fill_ = new brls::Box();
        fill_->setHeight(4);
        fill_->setWidth(0);
        fill_->setCornerRadius(2);
        fill_->setBackgroundColor(themeColor("brls/accent"));
        track_->addView(fill_);
        right->addView(track_);
        addView(right);
    }

    void show(const core::DownloadRow& row, app::AppContext& context) {
        title_->setText(row.mangaTitle);
        chapter_->setText(row.chapterName);
        state_->setText(stateText(row));
        state_->setTextColor(themeColor("brls/text"));  // "Failed after 3 tries" must read as well as the rest
        const bool bar = row.state == core::DownloadState::Downloading || (row.state == core::DownloadState::Queued &&
                                                                          row.progress > 0);
        track_->setVisibility(bar ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        fill_->setWidth(kBarWidth * static_cast<float>(std::clamp(row.progress, 0.0, 1.0)));
        const std::string url = row.thumbnailUrl.empty() ? std::string() : context.client.absoluteUrl(row.thumbnailUrl);
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
    CoverImage* cover_;
    brls::Label* title_;
    brls::Label* chapter_;
    brls::Label* state_;
    brls::Box* track_;
    brls::Box* fill_;
    std::string coverUrl_;
};

}  // namespace

ServerDownloadsTab::ServerDownloadsTab(app::AppContext& context, std::function<void(const core::Manga&)> onMangaChanged)
    : context_(context), downloads_(context.downloads), onMangaChanged_(std::move(onMangaChanged)) {
    setAxis(brls::Axis::COLUMN);
    setWidth(brls::View::AUTO);
    setHeight(brls::View::AUTO);
    buildHeader();
    status_ = new StatusView();
    status_->setGrow(1.0f);
    addView(status_);
    grid_ = new CoverGrid(kMetrics);
    grid_->setGrow(1.0f);
    grid_->setVisibility(brls::Visibility::GONE);
    addView(grid_);
    setDefaultFocusedIndex(2);  // from the tab list, the focus lands on the rows (the header's buttons are above)

    registerAction("server_downloads/pause"_i18n, brls::BUTTON_X, [this](brls::View*) {
        primaryAction();
        return true;
    });
    registerAction("server_downloads/open_manga"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        if (gridShown_) {
            openManga(grid_->focusIndex());
        }
        return true;
    });
    registerAction("server_downloads/menu"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        openMenu();
        return true;
    });

    listener_ = downloads_.addListener([this](const ServerDownloads::Change& change) { queueChanged(change); });
    grid_->setSource(this);
    watcher_ = downloads_.watch();  // one read at once: a queue started in the web UI shows too
    queueChanged(ServerDownloads::Change{});
}

ServerDownloadsTab::~ServerDownloadsTab() {
    grid_->setSource(nullptr);  // unbinds the cells (cancels their cover loads) while this tab still exists
    downloads_.removeListener(listener_);
    if (watcher_ != 0) {
        downloads_.unwatch(watcher_);
    }
}

void ServerDownloadsTab::setCovered(bool covered) {
    if (covered == covered_) {
        return;
    }
    covered_ = covered;
    if (covered && watcher_ != 0) {
        downloads_.unwatch(watcher_);
        watcher_ = 0;
    } else if (!covered && watcher_ == 0) {
        watcher_ = downloads_.watch();
    }
}

void ServerDownloadsTab::buildHeader() {
    auto* header = new brls::Box(brls::Axis::ROW);
    header->setHeight(96);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(12, 24, 12, 24);
    primaryButton_ = new brls::Button();
    primaryButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    primaryButton_->setHeight(72);
    primaryButton_->setWidth(180);
    primaryButton_->setText("server_downloads/pause"_i18n);
    primaryButton_->setId("server_downloads/primary");
    primaryButton_->registerClickAction([this](brls::View*) {
        primaryAction();
        return true;
    });
    header->addView(primaryButton_);
    clearButton_ = new brls::Button();
    clearButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    clearButton_->setHeight(72);
    clearButton_->setWidth(150);
    clearButton_->setMarginLeft(12);
    clearButton_->setText("server_downloads/clear"_i18n);
    clearButton_->setId("server_downloads/clear");
    clearButton_->registerClickAction([this](brls::View*) {
        askClear();
        return true;
    });
    header->addView(clearButton_);

    auto* column = new brls::Box(brls::Axis::COLUMN);
    column->setMarginLeft(20);
    column->setGrow(1.0f);
    statusLine_ = new brls::Label();
    statusLine_->setFontSize(16);
    statusLine_->setSingleLine(true);
    statusLine_->setAutoAnimate(false);
    column->addView(statusLine_);
    hintLine_ = new brls::Label();
    hintLine_->setFontSize(14);
    hintLine_->setMarginTop(6);
    hintLine_->setSingleLine(true);
    hintLine_->setAutoAnimate(false);
    hintLine_->setTextColor(kSecondaryText);
    column->addView(hintLine_);
    header->addView(column);
    addView(header);
}

void ServerDownloadsTab::queueChanged(const ServerDownloads::Change& change) {
    const double start = steadyMs();
    if (auto error = downloads_.takeError()) {
        brls::Application::notify(error->kind == api::ApiErrorKind::Permission ? "server_downloads/permission"_i18n
                                                                               : errorText(*error));
    }
    (void)change;
    const auto& queue = downloads_.queue();
    const std::size_t before = rows_.size();
    const std::size_t focus = grid_->focusIndex();
    rows_ = queue ? queue->rows : std::vector<core::DownloadRow>();
    if (!queue && downloads_.pollFailed()) {
        status_->showError("server_downloads/idle_title"_i18n, downloads_.pollError(), [this] { downloads_.refresh(); });
        showGrid(false);
    } else if (!queue) {
        status_->showLoading("server_downloads/loading"_i18n);
        showGrid(false);
    } else if (rows_.empty()) {
        status_->showMessage("server_downloads/idle_title"_i18n, "server_downloads/idle_text"_i18n);
        showGrid(false);
    } else {
        const bool wasShown = gridShown_;
        showGrid(true);
        if (!wasShown || rows_.size() != before) {
            // A row that left keeps the focus at the same place (the next row moves up into it).
            grid_->reload(std::min(focus, rows_.size() - 1));
        } else {
            grid_->rebind();
        }
    }
    updateHeader();
    report(steadyMs() - start + static_cast<double>(downloads_.applyMs()));
}

void ServerDownloadsTab::showGrid(bool visible) {
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
        if (hadFocus) {
            brls::Application::giveFocus(status_);  // first visible, then the focus (a hidden view refuses it, D42)
        }
    }
}

void ServerDownloadsTab::updateHeader() {
    const auto& queue = downloads_.queue();
    std::string line;
    std::string hint;
    const core::QueueView view = downloads_.view();
    if (downloads_.pollFailed()) {
        line = "server_downloads/cant_reach"_i18n;
    } else if (!queue) {
        line = "server_downloads/loading"_i18n;
    } else {
        const core::DownloadQueue& q = *queue;
        switch (view) {
            case core::QueueView::Idle:
                // Nothing at all: the message in the middle says it, the header stays empty.
                line = q.failed > 0 ? brls::getStr("server_downloads/stopped_failed", q.failed) : std::string();
                break;
            case core::QueueView::Starting:
                line = "server_downloads/starting"_i18n;
                break;
            case core::QueueView::Running:
                line = q.failed > 0 ? brls::getStr("server_downloads/running_failed", q.total(), q.failed)
                                    : brls::getStr("server_downloads/running", q.total());
                break;
            case core::QueueView::Paused:
                line = brls::getStr("server_downloads/paused", q.queued + q.downloading);
                hint = "server_downloads/paused_hint"_i18n;
                break;
        }
        if (q.leftOut > 0) {
            line += " · " + brls::getStr("server_downloads/more", q.leftOut);
        }
    }
    statusLine_->setText(line);
    hintLine_->setText(hint);
    // The first button: Pause while it runs, Resume when paused, Retry failed when only failed rows wait.
    const bool onlyFailed = queue && !queue->waiting() && queue->failed > 0;
    const char* key = view == core::QueueView::Paused ? "server_downloads/resume"
                      : onlyFailed                    ? "server_downloads/retry_failed"
                                                      : "server_downloads/pause";
    if (primaryKey_ != key) {
        primaryKey_ = key;
        primaryButton_->setText(brls::getStr(key));
        updateActionHint(brls::BUTTON_X, brls::getStr(key));
        brls::Application::getGlobalHintsUpdateEvent()->fire();  // borealis redraws the footer only when told
    }
    showButtons(queue && queue->total() > 0);
}

void ServerDownloadsTab::showButtons(bool shown) {
    // An empty queue has nothing to pause, retry or clear, and no row to open: the buttons and X / Y go away.
    const int now = shown ? 1 : 0;
    if (now == buttonsShown_) {
        return;
    }
    buttonsShown_ = now;
    brls::View* focus = brls::Application::getCurrentFocus();
    const bool hadFocus = inside(focus, primaryButton_) || inside(focus, clearButton_);
    const brls::Visibility visibility = shown ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE;
    primaryButton_->setVisibility(visibility);
    clearButton_->setVisibility(visibility);
    if (!shown && hadFocus) {
        brls::Application::giveFocus(gridShown_ ? static_cast<brls::View*>(grid_) : static_cast<brls::View*>(status_));
    }
    setActionAvailable(brls::BUTTON_X, shown);  // fires the footer's update
    setActionAvailable(brls::BUTTON_Y, shown);
}

void ServerDownloadsTab::primaryAction() {
    const auto& queue = downloads_.queue();
    if (!queue) {
        return;
    }
    if (downloads_.busy()) {
        downloads_.pause();
    } else if (queue->queued > 0) {
        downloads_.resume();
    } else if (queue->failed > 0) {
        downloads_.retryFailed();
    }
}

void ServerDownloadsTab::askClear() {
    const auto& queue = downloads_.queue();
    if (!queue || queue->total() == 0) {
        return;
    }
    const int total = queue->total();
    const std::string text = total == 1 ? "server_downloads/clear_ask_one"_i18n
                                        : brls::getStr("server_downloads/clear_ask", total);
    auto* dialog = new brls::Dialog(text);
    dialog->addButton("server_downloads/cancel"_i18n, [] {});
    dialog->addButton("server_downloads/clear_yes"_i18n, [this] { downloads_.clear(); });
    dialog->open();
    app::testhooks::write("dialog", nlohmann::ordered_json{{"text", text}});
}

void ServerDownloadsTab::openRowMenu(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    const core::DownloadRow row = rows_[index];
    std::vector<std::string> entries = {"server_downloads/move_top"_i18n, "server_downloads/move_bottom"_i18n,
                                        "server_downloads/remove"_i18n};
    const bool failed = row.state == core::DownloadState::Error;
    if (failed) {
        entries.push_back("server_downloads/retry"_i18n);
    }
    entries.push_back("server_downloads/open_manga"_i18n);
    auto* dropdown = new brls::Dropdown(row.chapterName, entries, [this, row, failed, index](int selected) {
        brls::sync([this, row, failed, index, selected] {
            const int open = failed ? 4 : 3;
            if (selected == 0) {
                downloads_.moveToTop(row.chapterId);
            } else if (selected == 1) {
                downloads_.moveToBottom(row.chapterId);
            } else if (selected == 2) {
                downloads_.removeFromQueue(row.chapterId);
            } else if (failed && selected == 3) {
                downloads_.retry(row.chapterId);
            } else if (selected == open) {
                openManga(index);
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ServerDownloadsTab::openMenu() {
    const auto& queue = downloads_.queue();
    const bool paused = downloads_.view() == core::QueueView::Paused;
    std::vector<std::string> entries = {paused ? "server_downloads/resume"_i18n : "server_downloads/pause"_i18n,
                                        "server_downloads/retry_failed"_i18n, "server_downloads/menu_clear"_i18n,
                                        "server_downloads/menu_refresh"_i18n};
    (void)queue;
    auto* dropdown = new brls::Dropdown("server_downloads/menu_title"_i18n, entries, [this, paused](int selected) {
        brls::sync([this, paused, selected] {  // the menu closes first; the action runs on the following frame
            if (selected == 0) {
                if (paused) {
                    downloads_.resume();
                } else {
                    downloads_.pause();
                }
            } else if (selected == 1) {
                downloads_.retryFailed();
            } else if (selected == 2) {
                askClear();
            } else if (selected == 3) {
                downloads_.refresh();
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void ServerDownloadsTab::openManga(std::size_t index) {
    if (index >= rows_.size() || rows_[index].mangaId <= 0) {
        return;
    }
    core::Manga manga;
    manga.id = rows_[index].mangaId;
    manga.title = rows_[index].mangaTitle;
    manga.thumbnailUrl = rows_[index].thumbnailUrl;
    brls::Application::pushActivity(new MangaActivity(context_, manga, onMangaChanged_));
}

std::size_t ServerDownloadsTab::count() const {
    return gridShown_ ? rows_.size() : 0;
}

GridCell* ServerDownloadsTab::makeCell() {
    return new DownloadCell();
}

void ServerDownloadsTab::bind(GridCell& cell, std::size_t index) {
    cell.setId("server_downloads/row-" + std::to_string(index));  // the "focus" test event
    static_cast<DownloadCell&>(cell).show(rows_[index], context_);
}

void ServerDownloadsTab::unbind(GridCell& cell) {
    static_cast<DownloadCell&>(cell).clear();
}

void ServerDownloadsTab::select(std::size_t index) {
    openRowMenu(index);
}

void ServerDownloadsTab::report(double applyMs) {
    const auto& queue = downloads_.queue();
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["view"] = queue ? core::queueViewName(downloads_.view()) : "loading";
    fields["started"] = queue && queue->started;
    fields["rows"] = queue ? queue->total() : 0;
    fields["failed"] = queue ? queue->failed : 0;
    fields["leftOut"] = queue ? queue->leftOut : 0;
    fields["pollFailed"] = downloads_.pollFailed();
    nlohmann::ordered_json first = nlohmann::ordered_json::array();
    for (std::size_t i = 0; i < rows_.size() && i < 10; ++i) {
        const core::DownloadRow& row = rows_[i];
        first.push_back({{"chapterId", row.chapterId},
                         {"chapter", row.chapterName},
                         {"mangaId", row.mangaId},
                         {"manga", row.mangaTitle},
                         {"state", stateName(row.state)},
                         {"percent", static_cast<int>(row.progress * 100.0 + 1e-6)},
                         {"tries", row.tries}});
    }
    fields["first"] = first;
    // polls is in the key, so every answer the tab applies writes one event (tests count the polls with it: none while the
    // tab is closed). ms is not: the same answer applied again writes nothing.
    fields["polls"] = downloads_.polls();
    const std::string key = fields.dump();
    if (key == reported_) {
        return;
    }
    reported_ = key;
    fields["ms"] = static_cast<int>(applyMs + 0.5);
    app::testhooks::write("server_downloads", fields);
}

}  // namespace snx::ui
