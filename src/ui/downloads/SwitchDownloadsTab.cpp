#include "ui/downloads/SwitchDownloadsTab.hpp"

#include <algorithm>
#include <utility>

#include "app/TestHooks.hpp"
#include "core/ServerDownloads.hpp"
#include "platform/Platform.hpp"
#include "ui/components/CoverImage.hpp"
#include "ui/manga/MangaActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of rows, the size of the Server downloads rows (WP-17): far over the 72 px touch minimum (D16).
constexpr float kRowWidth = 800;
constexpr float kRowHeight = 96;
constexpr GridMetrics kMetrics{kRowWidth, kRowHeight, 8, 12};
constexpr float kCoverWidth = 60;
constexpr float kCoverHeight = 90;
constexpr float kStateWidth = 220;
constexpr float kBarWidth = 200;
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

double fraction(const SwitchDownloads::ChapterStatus& status) {
    return status.pages > 0 ? static_cast<double>(status.pagesDone) / status.pages : 0.0;
}

// "Queued", "Downloading 40 %", "Paused at 40 %", "Failed at page 2 of 10", "Failed".
std::string stateText(const core::SwitchQueueEntry& entry, const SwitchDownloads::ChapterStatus& status) {
    switch (status.state) {
        case SwitchDownloads::ChapterState::Downloading:
            return brls::getStr("switch_downloads/state_downloading", core::percentText(fraction(status)));
        case SwitchDownloads::ChapterState::Paused:
            return brls::getStr("switch_downloads/state_paused", core::percentText(fraction(status)));
        case SwitchDownloads::ChapterState::Failed:
            if (status.failedPage >= 0 && status.pages > 0) {
                return brls::getStr("switch_downloads/state_failed_page", status.failedPage + 1, status.pages);
            }
            return "switch_downloads/state_failed"_i18n;
        default:
            break;
    }
    (void)entry;
    return "switch_downloads/state_queued"_i18n;
}

// The second line of a manga row: "3 chapters · 1.8 MB", "1 chapter · 12 MB".
std::string mangaText(const SwitchDownloads::MangaRow& row) {
    if (row.damaged) {
        return "switch_downloads/manga_unreadable"_i18n;
    }
    return row.chapters == 1 ? brls::getStr("switch_downloads/manga_chapter_one", core::bytesText(row.bytes))
                             : brls::getStr("switch_downloads/manga_chapters", row.chapters, core::bytesText(row.bytes));
}

// One row of any kind: the cover, two lines of text, the state and a thin progress bar on the right.
class SwitchCell : public GridCell {
  public:
    SwitchCell() {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);

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
        detail_ = new brls::Label();
        detail_->setWidth(textWidth);
        detail_->setFontSize(15);
        detail_->setMarginTop(6);
        detail_->setSingleLine(true);
        detail_->setAutoAnimate(false);
        detail_->setTextColor(kSecondaryText);
        texts->addView(detail_);
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

    void showEntry(const core::SwitchQueueEntry& entry, const SwitchDownloads::ChapterStatus& status,
                   app::AppContext& context) {
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));
        cover_->setVisibility(brls::Visibility::VISIBLE);
        title_->setText(entry.mangaTitle);
        detail_->setText(entry.chapter.name);
        state_->setText(stateText(entry, status));
        state_->setTextColor(themeColor("brls/text"));
        const bool bar = status.state == SwitchDownloads::ChapterState::Downloading ||
                         status.state == SwitchDownloads::ChapterState::Paused;
        track_->setVisibility(bar ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
        fill_->setWidth(kBarWidth * static_cast<float>(std::clamp(fraction(status), 0.0, 1.0)));
        loadCover(entry.thumbnailUrl.empty() ? std::string() : context.client.absoluteUrl(entry.thumbnailUrl), context);
    }

    void showHeader() {
        setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        cover_->setVisibility(brls::Visibility::INVISIBLE);
        clear();
        title_->setText("switch_downloads/on_switch"_i18n);
        detail_->setText("");
        state_->setText("");
        track_->setVisibility(brls::Visibility::INVISIBLE);
    }

    void showManga(const SwitchDownloads::MangaRow& row, app::AppContext& context) {
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));
        cover_->setVisibility(brls::Visibility::VISIBLE);
        title_->setText(row.title);
        detail_->setText(mangaText(row));
        state_->setText(row.damagedChapters > 0 ? brls::getStr("switch_downloads/manga_damaged", row.damagedChapters)
                                                : std::string());
        state_->setTextColor(themeColor("brls/text"));
        track_->setVisibility(brls::Visibility::INVISIBLE);
        // The cover on the SD card (no request); without one, the server's while it answers.
        const std::string url = !row.coverFile.empty()    ? "file:" + row.coverFile
                                : row.thumbnailUrl.empty() ? std::string()
                                                           : context.client.absoluteUrl(row.thumbnailUrl);
        loadCover(url, context);
    }

    void clear() {
        cover_->clear();
        coverUrl_.clear();
    }

  private:
    void loadCover(const std::string& url, app::AppContext& context) {
        if (url == coverUrl_) {
            return;
        }
        coverUrl_ = url;
        if (url.empty()) {
            cover_->clear();
            return;
        }
        cover_->load(url, context);
    }

    CoverImage* cover_;
    brls::Label* title_;
    brls::Label* detail_;
    brls::Label* state_;
    brls::Box* track_;
    brls::Box* fill_;
    std::string coverUrl_;
};

}  // namespace

SwitchDownloadsTab::SwitchDownloadsTab(app::AppContext& context, std::function<void(const core::Manga&)> onMangaChanged)
    : context_(context), downloads_(context.switchDownloads), onMangaChanged_(std::move(onMangaChanged)) {
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

    registerAction("switch_downloads/pause"_i18n, brls::BUTTON_X, [this](brls::View*) {
        primaryAction();
        return true;
    });
    registerAction("switch_downloads/y_action"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        yAction();
        return true;
    });
    registerAction("switch_downloads/menu"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        openMenu();
        return true;
    });

    listener_ = downloads_.addListener([this](const SwitchDownloads::Change& change) { changed(change); });
    grid_->setSource(this);
    changed(SwitchDownloads::Change{});
}

SwitchDownloadsTab::~SwitchDownloadsTab() {
    grid_->setSource(nullptr);  // unbinds the cells (cancels their cover loads) while this tab still exists
    downloads_.removeListener(listener_);
    if (awake_) {
        platform::keepScreenOn(false);
    }
}

void SwitchDownloadsTab::setCovered(bool covered) {
    covered_ = covered;
    updateKeepAwake();
}

void SwitchDownloadsTab::updateKeepAwake() {
    // On only while it downloads and this tab is what the user sees (the reader keeps its own; it covers this tab).
    const bool want = !covered_ && downloads_.view() == core::SwitchQueueView::Running;
    if (want != awake_) {
        awake_ = want;
        platform::keepScreenOn(want);
        brls::Logger::info("switch downloads: keep awake {}", want ? "on" : "off");
    }
}

void SwitchDownloadsTab::buildHeader() {
    auto* header = new brls::Box(brls::Axis::ROW);
    header->setHeight(96);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(12, 24, 12, 24);
    primaryButton_ = new brls::Button();
    primaryButton_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    primaryButton_->setHeight(72);
    primaryButton_->setWidth(180);
    primaryButton_->setText("switch_downloads/pause"_i18n);
    primaryButton_->setId("switch_downloads/primary");
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
    clearButton_->setText("switch_downloads/clear"_i18n);
    clearButton_->setId("switch_downloads/clear");
    clearButton_->registerClickAction([this](brls::View*) {
        askClear();
        return true;
    });
    header->addView(clearButton_);

    auto* column = new brls::Box(brls::Axis::COLUMN);
    column->setMarginLeft(20);
    column->setGrow(1.0f);
    spaceLine_ = new brls::Label();
    spaceLine_->setFontSize(16);
    spaceLine_->setSingleLine(true);
    spaceLine_->setAutoAnimate(false);
    column->addView(spaceLine_);
    stateLine_ = new brls::Label();
    stateLine_->setFontSize(14);
    stateLine_->setMarginTop(6);
    stateLine_->setSingleLine(true);
    stateLine_->setAutoAnimate(false);
    stateLine_->setTextColor(kSecondaryText);
    column->addView(stateLine_);
    header->addView(column);
    addView(header);
}

void SwitchDownloadsTab::rebuildItems() {
    items_.clear();
    for (const core::SwitchQueueEntry& entry : downloads_.queue().entries) {
        Item item;
        item.kind = Item::Kind::Entry;
        item.entry = entry;
        item.status = downloads_.chapterState(entry.chapter.id);
        items_.push_back(std::move(item));
    }
    const std::vector<SwitchDownloads::MangaRow> manga = downloads_.downloadedManga();
    if (!manga.empty()) {
        Item header;
        header.kind = Item::Kind::Header;
        items_.push_back(std::move(header));
    }
    for (const SwitchDownloads::MangaRow& row : manga) {
        Item item;
        item.kind = Item::Kind::Manga;
        item.manga = row;
        items_.push_back(std::move(item));
    }
}

void SwitchDownloadsTab::changed(const SwitchDownloads::Change& change) {
    (void)change;
    if (auto error = downloads_.takeError()) {
        brls::Application::notify(*error);
    }
    const std::size_t before = items_.size();
    const std::size_t focus = grid_->focusIndex();
    rebuildItems();
    if (!downloads_.loaded()) {
        status_->showLoading("switch_downloads/loading"_i18n);
        showGrid(false);
    } else if (items_.empty()) {
        status_->showMessage("switch_downloads/idle_title"_i18n, "switch_downloads/idle_text"_i18n);
        showGrid(false);
    } else {
        const bool wasShown = gridShown_;
        showGrid(true);
        if (!wasShown || items_.size() != before) {
            // A row that left keeps the focus at the same place (the next row moves up into it).
            grid_->reload(std::min(focus, items_.size() - 1));
        } else {
            grid_->rebind();
        }
    }
    updateHeader();
    updateKeepAwake();
}

void SwitchDownloadsTab::showGrid(bool visible) {
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

void SwitchDownloadsTab::updateHeader() {
    // The space: "2.3 GB of 8 GB used · 40 GB free on the SD card".
    const SwitchDownloads::Space space = downloads_.space();
    std::string spaceText;
    if (downloads_.loaded()) {
        const std::string used = core::bytesText(space.used);
        if (space.limit > 0 && space.free) {
            spaceText = brls::getStr("switch_downloads/space_limit", used, core::bytesText(space.limit),
                                     core::bytesText(*space.free));
        } else if (space.limit > 0) {
            spaceText = brls::getStr("switch_downloads/space_limit_no_free", used, core::bytesText(space.limit));
        } else if (space.free) {
            spaceText = brls::getStr("switch_downloads/space_no_limit", used, core::bytesText(*space.free));
        } else {
            spaceText = brls::getStr("switch_downloads/space_used", used);
        }
    }
    spaceLine_->setText(spaceText);

    // The queue's state.
    const int entries = static_cast<int>(downloads_.queue().entries.size());
    const int failed = downloads_.failedCount();
    const int waiting = entries - failed;
    const core::SwitchQueueView view = downloads_.view();
    std::string line;
    switch (view) {
        case core::SwitchQueueView::Idle:
            break;
        case core::SwitchQueueView::Running:
            line = failed > 0 ? brls::getStr("switch_downloads/running_failed", waiting, failed)
                              : brls::getStr("switch_downloads/running", waiting);
            break;
        case core::SwitchQueueView::Paused:
            line = brls::getStr("switch_downloads/paused", waiting);
            break;
        case core::SwitchQueueView::Waiting:
            line = brls::getStr(std::string("switch_downloads/wait_") + core::switchWaitName(downloads_.wait()));
            break;
        case core::SwitchQueueView::Stopped:
            line = brls::getStr("switch_downloads/stopped_failed", failed);
            break;
    }
    stateLine_->setText(line);

    const bool onlyFailed = waiting <= 0 && failed > 0;
    const char* key = view == core::SwitchQueueView::Paused ? "switch_downloads/resume"
                      : onlyFailed                          ? "switch_downloads/retry_failed"
                                                            : "switch_downloads/pause";
    if (primaryKey_ != key) {
        primaryKey_ = key;
        primaryButton_->setText(brls::getStr(key));
        updateActionHint(brls::BUTTON_X, brls::getStr(key));
        brls::Application::getGlobalHintsUpdateEvent()->fire();  // borealis redraws the footer only when told
    }
    showButtons(entries > 0);
}

void SwitchDownloadsTab::showButtons(bool shown) {
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
    setActionAvailable(brls::BUTTON_X, shown);
}

void SwitchDownloadsTab::primaryAction() {
    const core::SwitchQueueView view = downloads_.view();
    if (view == core::SwitchQueueView::Paused) {
        downloads_.resume();
    } else if (view == core::SwitchQueueView::Stopped) {
        downloads_.retryFailed();
    } else if (!downloads_.queue().entries.empty()) {
        downloads_.pause();
    }
}

void SwitchDownloadsTab::askClear() {
    const int total = static_cast<int>(downloads_.queue().entries.size());
    if (total == 0) {
        return;
    }
    const std::string text = total == 1 ? "switch_downloads/clear_ask_one"_i18n
                                        : brls::getStr("switch_downloads/clear_ask", total);
    auto* dialog = new brls::Dialog(text);
    dialog->addButton("switch_downloads/cancel"_i18n, [] {});
    dialog->addButton("switch_downloads/clear_yes"_i18n, [this] { downloads_.clear(); });
    dialog->open();
    app::testhooks::write("dialog", nlohmann::ordered_json{{"text", text}});
}

void SwitchDownloadsTab::openEntryMenu(std::size_t index) {
    if (index >= items_.size() || items_[index].kind != Item::Kind::Entry) {
        return;
    }
    const core::SwitchQueueEntry entry = items_[index].entry;
    const bool failed = items_[index].status.state == SwitchDownloads::ChapterState::Failed;
    std::vector<std::string> names = {"switch_downloads/move_top"_i18n, "switch_downloads/move_bottom"_i18n,
                                      "switch_downloads/remove"_i18n};
    if (failed) {
        names.push_back("switch_downloads/retry"_i18n);
    }
    names.push_back("switch_downloads/open_manga"_i18n);
    auto* dropdown = new brls::Dropdown(entry.chapter.name, names, [this, entry, failed](int selected) {
        brls::sync([this, entry, failed, selected] {
            const int open = failed ? 4 : 3;
            if (selected == 0) {
                downloads_.moveToTop(entry.chapter.id);
            } else if (selected == 1) {
                downloads_.moveToBottom(entry.chapter.id);
            } else if (selected == 2) {
                downloads_.removeFromQueue(entry.chapter.id);
            } else if (failed && selected == 3) {
                downloads_.retry(entry.chapter.id);
            } else if (selected == open) {
                openManga(entry.mangaId, entry.mangaTitle, entry.thumbnailUrl, false);
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void SwitchDownloadsTab::openMangaMenu(std::size_t index) {
    if (index >= items_.size() || items_[index].kind != Item::Kind::Manga) {
        return;
    }
    const SwitchDownloads::MangaRow row = items_[index].manga;
    std::vector<std::string> names = {"switch_downloads/open"_i18n, "switch_downloads/delete_all"_i18n};
    auto* dropdown = new brls::Dropdown(row.title, names, [this, row](int selected) {
        brls::sync([this, row, selected] {
            if (selected == 0) {
                openManga(row.mangaId, row.title, row.thumbnailUrl, true);
            } else if (selected == 1) {
                askDeleteManga(row);
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void SwitchDownloadsTab::askDeleteManga(const SwitchDownloads::MangaRow& row) {
    const std::string text = row.damaged ? brls::getStr("switch_downloads/delete_ask_damaged", row.title)
                             : row.chapters == 1
                                 ? brls::getStr("switch_downloads/delete_ask_one", row.title)
                                 : brls::getStr("switch_downloads/delete_ask", row.chapters, row.title);
    const int mangaId = row.mangaId;
    auto* dialog = new brls::Dialog(text);
    dialog->addButton("switch_downloads/cancel"_i18n, [] {});
    dialog->addButton("switch_downloads/delete_yes"_i18n, [this, mangaId] { downloads_.deleteManga(mangaId); });
    dialog->open();
    app::testhooks::write("dialog", nlohmann::ordered_json{{"text", text}});
}

void SwitchDownloadsTab::openMenu() {
    const bool paused = downloads_.view() == core::SwitchQueueView::Paused;
    std::vector<std::string> names = {paused ? "switch_downloads/resume"_i18n : "switch_downloads/pause"_i18n,
                                      "switch_downloads/retry_failed"_i18n, "switch_downloads/menu_clear"_i18n};
    auto* dropdown = new brls::Dropdown("switch_downloads/menu_title"_i18n, names, [this, paused](int selected) {
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
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void SwitchDownloadsTab::openManga(int mangaId, const std::string& title, const std::string& thumbnailUrl, bool onSwitch) {
    if (mangaId <= 0) {
        return;
    }
    core::Manga manga;
    manga.id = mangaId;
    manga.title = title;
    manga.thumbnailUrl = thumbnailUrl;
    auto* activity = new MangaActivity(context_, manga, onMangaChanged_);
    if (onSwitch) {
        activity->showOnSwitch();
    }
    brls::Application::pushActivity(activity);
}

void SwitchDownloadsTab::yAction() {
    if (!gridShown_) {
        return;
    }
    const std::size_t index = grid_->focusIndex();
    if (index >= items_.size()) {
        return;
    }
    const Item& item = items_[index];
    if (item.kind == Item::Kind::Entry) {
        openManga(item.entry.mangaId, item.entry.mangaTitle, item.entry.thumbnailUrl, false);
    } else if (item.kind == Item::Kind::Manga) {
        openMangaMenu(index);
    }
}

std::size_t SwitchDownloadsTab::count() const {
    return gridShown_ ? items_.size() : 0;
}

GridCell* SwitchDownloadsTab::makeCell() {
    return new SwitchCell();
}

void SwitchDownloadsTab::bind(GridCell& cell, std::size_t index) {
    const Item& item = items_[index];
    auto& row = static_cast<SwitchCell&>(cell);
    switch (item.kind) {
        case Item::Kind::Entry:
            cell.setId("switch_downloads/row-" + std::to_string(index));  // the "focus" test event
            row.showEntry(item.entry, item.status, context_);
            break;
        case Item::Kind::Header:
            cell.setId("switch_downloads/header");
            row.showHeader();
            break;
        case Item::Kind::Manga:
            cell.setId("switch_downloads/manga-" + std::to_string(item.manga.mangaId));
            row.showManga(item.manga, context_);
            break;
    }
}

void SwitchDownloadsTab::unbind(GridCell& cell) {
    static_cast<SwitchCell&>(cell).clear();
}

void SwitchDownloadsTab::select(std::size_t index) {
    if (index >= items_.size()) {
        return;
    }
    const Item& item = items_[index];
    if (item.kind == Item::Kind::Entry) {
        openEntryMenu(index);
    } else if (item.kind == Item::Kind::Manga) {
        if (item.manga.damaged) {
            openMangaMenu(index);  // nothing to open: only "Delete all downloads..." works
        } else {
            openManga(item.manga.mangaId, item.manga.title, item.manga.thumbnailUrl, true);
        }
    }
}

}  // namespace snx::ui
