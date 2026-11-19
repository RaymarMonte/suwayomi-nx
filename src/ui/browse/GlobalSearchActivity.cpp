#include "ui/browse/GlobalSearchActivity.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "app/ConfigStore.hpp"
#include "app/TestHooks.hpp"
#include "core/LibraryTools.hpp"
#include "ui/browse/AddFlow.hpp"
#include "ui/browse/SourceActivity.hpp"
#include "ui/components/AskText.hpp"
#include "ui/components/CoverCell.hpp"
#include "ui/components/CoverImage.hpp"
#include "ui/components/TextFit.hpp"
#include "ui/manga/MangaActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr float kSpacing = 16;
constexpr float kPadding = 24;
constexpr float kCellHeight = kCoverGridMetrics.cellHeight;  // a cover with its title (D42)
constexpr float kCardPadding = 12;
constexpr float kIconSide = 64;
constexpr float kNameFontSize = 18;
constexpr float kStateFontSize = 16;
constexpr float kLineHeight = 1.25f;
const NVGcolor kSecondaryText = nvgRGB(150, 150, 156);
const NVGcolor kCardBackground = nvgRGBA(128, 128, 128, 24);

std::string displayName(const core::Source& source) {
    return source.displayName.empty() ? source.name : source.displayName;
}

// What the card says about its source's search.
std::string stateText(const GlobalSearchModel::Row& row) {
    switch (row.state) {
        case GlobalSearchModel::RowState::Waiting:
            return "browse/search_all_waiting"_i18n;
        case GlobalSearchModel::RowState::Searching:
            return "browse/search_all_searching"_i18n;
        case GlobalSearchModel::RowState::Found:
            if (row.more) {
                return brls::getStr("browse/search_all_found_more", row.found);
            }
            return row.found == 1 ? "browse/search_all_found_one"_i18n : brls::getStr("browse/search_all_found", row.found);
        case GlobalSearchModel::RowState::NothingFound:
            return "browse/nothing_found"_i18n;
        case GlobalSearchModel::RowState::Failed:
            return brls::getStr("browse/search_all_failed", errorText(row.error));
    }
    return {};
}

// One place in the grid: a source's card (column 0), one of its covers, or nothing (an empty place). The grid reuses
// cells for any place, so a cell holds both kinds and shows one.
class SearchCell : public GridCell {
  public:
    explicit SearchCell(float width) : width_(width) {
        setAxis(brls::Axis::ROW);
        setJustifyContent(brls::JustifyContent::CENTER);
        setAlignItems(brls::AlignItems::FLEX_START);
        setCornerRadius(4);
        setHighlightCornerRadius(6);

        card_ = new brls::Box(brls::Axis::COLUMN);
        card_->setWidth(width);
        card_->setHeight(kCellHeight);
        card_->setAlignItems(brls::AlignItems::CENTER);
        card_->setPadding(16, kCardPadding, kCardPadding, kCardPadding);
        card_->setCornerRadius(4);
        card_->setBackgroundColor(kCardBackground);
        icon_ = new CoverImage(kIconSide, kIconSide);
        card_->addView(icon_);
        name_ = new brls::Label();
        name_->setWidth(width - 2 * kCardPadding);
        name_->setMarginTop(12);
        name_->setFontSize(kNameFontSize);
        name_->setLineHeight(kLineHeight);
        name_->setIsWrapping(true);
        name_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        name_->setAutoAnimate(false);
        card_->addView(name_);
        state_ = new brls::Label();
        state_->setWidth(width - 2 * kCardPadding);
        state_->setMarginTop(8);
        state_->setFontSize(kStateFontSize);
        state_->setLineHeight(kLineHeight);
        state_->setIsWrapping(true);
        state_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        state_->setTextColor(kSecondaryText);
        state_->setAutoAnimate(false);
        card_->addView(state_);
        addView(card_);

        cover_ = new CoverCell();
        cover_->setWidth(kCoverWidth);
        cover_->setHeight(kCellHeight);
        addView(cover_);
    }

    void showCard(const GlobalSearchModel::Row& row, app::AppContext& context) {
        cover_->clear();
        cover_->setVisibility(brls::Visibility::GONE);
        card_->setVisibility(brls::Visibility::VISIBLE);
        const float textWidth = width_ - 2 * kCardPadding;
        name_->setText(fitLines(displayName(row.source), textWidth, name_->getFont(), kNameFontSize, kLineHeight, 2).text);
        state_->setText(fitLines(stateText(row), textWidth, state_->getFont(), kStateFontSize, kLineHeight, 3).text);
        icon_->load(row.source.iconUrl.empty() ? std::string() : context.client.absoluteUrl(row.source.iconUrl), context);
    }

    void showCover(const core::Manga& manga, app::AppContext& context) {
        icon_->clear();
        card_->setVisibility(brls::Visibility::GONE);
        cover_->setVisibility(brls::Visibility::VISIBLE);
        const std::string url = manga.thumbnailUrl.empty() ? std::string() : context.client.absoluteUrl(manga.thumbnailUrl);
        cover_->show(manga, url, context, manga.inLibrary ? "browse/in_library"_i18n : std::string());
    }

    void showNothing() {
        clear();
        card_->setVisibility(brls::Visibility::GONE);
        cover_->setVisibility(brls::Visibility::GONE);
    }

    void clear() {
        icon_->clear();
        cover_->clear();
    }

  private:
    float width_;
    brls::Box* card_;
    CoverImage* icon_;
    brls::Label* name_;
    brls::Label* state_;
    CoverCell* cover_;
};

GlobalSearchModel::Limits limitsFrom(const core::RuntimeProfile& profile) {
    GlobalSearchModel::Limits limits;
    limits.concurrency = static_cast<std::size_t>(std::max(1, profile.globalSearchConcurrency));
    limits.covers = static_cast<std::size_t>(std::max(1, profile.globalSearchCovers));
    limits.sourcesMax = static_cast<std::size_t>(std::max(1, profile.globalSearchSourcesMax));
    return limits;
}

}  // namespace

GlobalSearchActivity::GlobalSearchActivity(app::AppContext& context, const std::vector<core::Source>& sources,
                                           const std::vector<std::string>& hiddenLanguages, std::string query,
                                           std::function<void(const core::Manga&)> onLibraryChanged)
    : context_(context),
      model_(context.client, sources, hiddenLanguages, limitsFrom(context.profile)),
      firstQuery_(std::move(query)),
      onLibraryChanged_(std::move(onLibraryChanged)) {}

GlobalSearchActivity::~GlobalSearchActivity() {
    alive_.cancel();
    if (grid_ != nullptr) {
        grid_->setSource(nullptr);  // unbinds the cells (cancels their loads) while this screen still exists
    }
    model_.setListener({});
}

std::size_t GlobalSearchActivity::columns() const {
    return 1 + model_.limits().covers;
}

void GlobalSearchActivity::onContentAvailable() {
    // Exactly 1 + covers columns over the screen's width (D56): the cell width follows from it.
    const auto columnCount = static_cast<float>(columns());
    const float width =
        std::floor((brls::Application::contentWidth - 2 * kPadding - (columnCount - 1) * kSpacing) / columnCount);
    const GridMetrics metrics{std::max(width, kCoverWidth), kCellHeight, kSpacing, kPadding, static_cast<int>(columns())};

    root->setDefaultFocusedIndex(1);  // the focus starts on the rows, not on the bar
    status_ = new StatusView();
    status_->setGrow(1.0f);
    status_->setVisibility(brls::Visibility::GONE);
    body->addView(status_);
    grid_ = new CoverGrid(metrics);
    grid_->setGrow(1.0f);
    body->addView(grid_);
    gridShown_ = true;

    searchButton->registerClickAction([this](brls::View*) {
        askSearch();
        return true;
    });
    brls::View* content = getContentView();
    content->registerAction("browse/search"_i18n, brls::BUTTON_X, [this](brls::View*) {
        askSearch();
        return true;
    });
    content->registerAction("browse/cover_menu"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        openCoverMenu(grid_->focusIndex());
        return true;
    });

    const net::CancelToken alive = alive_;
    flow_ = AddFlow::make(context_, [this, alive](const core::Manga& now) {
        if (!alive.cancelled()) {
            libraryChanged(now);
        }
    });
    ready_ = true;
    model_.setListener([this] { modelChanged(); });
    grid_->setSource(this);
    app::testhooks::screen("GlobalSearch");
    search(firstQuery_);
}

void GlobalSearchActivity::onResume() {
    app::testhooks::screen("GlobalSearch");
}

void GlobalSearchActivity::search(std::string query) {
    startedAtMs_ = app::appClockMs();
    reported_.clear();
    if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
        frame->setTitle(brls::getStr("browse/search_all_title", query));
    }
    if (!model_.search(std::move(query))) {
        modelChanged();
    }
}

void GlobalSearchActivity::askSearch() {
    const int maxChars = context_.profile.searchMaxChars;
    askText("browse/search_all_header"_i18n, model_.query(), maxChars, [this, maxChars](std::string text) {
        std::string trimmed = core::utf8Cut(core::trimText(text), static_cast<std::size_t>(maxChars));
        if (!trimmed.empty()) {
            search(std::move(trimmed));
        }
    });
}

void GlobalSearchActivity::modelChanged() {
    if (!ready_) {
        return;
    }
    const std::size_t total = model_.rows().size();
    if (total == 0) {
        // Nothing to search (no source in a shown language): a message in place of the grid.
        progress->setText("browse/search_all_none"_i18n);
        if (gridShown_) {
            gridShown_ = false;
            grid_->setVisibility(brls::Visibility::GONE);
            status_->setVisibility(brls::Visibility::VISIBLE);
            status_->showMessage("browse/search_all_none"_i18n, "browse/search_all_none_text"_i18n);
        }
        report();
        return;
    }
    progress->setText(model_.finished() ? brls::getStr("browse/search_all_done", total)
                                        : brls::getStr("browse/search_all_progress", model_.doneCount(), total));
    if (model_.listRevision() != shownList_) {
        grid_->reload(0);  // a new text: back to the first card
    } else if (model_.itemsRevision() != shownItems_) {
        grid_->rebind();  // answers came in (rows keep their places), or an "In library" tag changed
    }
    shownList_ = model_.listRevision();
    shownItems_ = model_.itemsRevision();
    // borealis starts the focus on the bar while the grid isn't laid out yet: once it is, the first card takes it.
    if (!startFocusMoved_ && brls::Application::getCurrentFocus() == searchButton) {
        brls::Application::giveFocus(grid_);
        startFocusMoved_ = brls::Application::getCurrentFocus() != searchButton;
    }
    report();
}

void GlobalSearchActivity::report() {
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (const GlobalSearchModel::Row& row : model_.rows()) {
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["id"] = row.source.id;
        entry["name"] = displayName(row.source);
        entry["state"] = std::string(globalSearchStateName(row.state));
        entry["found"] = row.found;
        entry["more"] = row.more;
        entry["shown"] = row.manga.size();
        nlohmann::ordered_json titles = nlohmann::ordered_json::array();
        nlohmann::ordered_json inLibrary = nlohmann::ordered_json::array();
        for (const core::Manga& m : row.manga) {
            titles.push_back(m.title);
            if (m.inLibrary) {
                inLibrary.push_back(m.id);
            }
        }
        entry["titles"] = titles;
        entry["inLibrary"] = inLibrary;
        if (row.state == GlobalSearchModel::RowState::Failed) {
            entry["message"] = errorText(row.error);
        }
        rows.push_back(entry);
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["query"] = model_.query();
    fields["total"] = model_.rows().size();
    fields["done"] = model_.doneCount();
    fields["running"] = model_.runningCount();
    fields["finished"] = model_.finished();
    fields["rows"] = rows;
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        fields["ms"] = app::appClockMs() - startedAtMs_;  // the text asked → now (not part of the "same" check)
        app::testhooks::write("global_search", fields);
    }
}

std::size_t GlobalSearchActivity::count() const {
    return model_.rows().size() * columns();
}

GridCell* GlobalSearchActivity::makeCell() {
    const auto columnCount = static_cast<float>(columns());
    const float width =
        std::floor((brls::Application::contentWidth - 2 * kPadding - (columnCount - 1) * kSpacing) / columnCount);
    return new SearchCell(std::max(width, kCoverWidth));
}

const core::Manga* GlobalSearchActivity::mangaAt(std::size_t index) const {
    const std::size_t row = index / columns();
    const std::size_t column = index % columns();
    if (row >= model_.rows().size() || column == 0 || column - 1 >= model_.rows()[row].manga.size()) {
        return nullptr;
    }
    return &model_.rows()[row].manga[column - 1];
}

bool GlobalSearchActivity::focusable(std::size_t index) const {
    return index % columns() == 0 || mangaAt(index) != nullptr;  // a card, or a cover that is there
}

void GlobalSearchActivity::bind(GridCell& cell, std::size_t index) {
    const std::size_t row = index / columns();
    const std::size_t column = index % columns();
    auto& place = static_cast<SearchCell&>(cell);
    if (row >= model_.rows().size()) {
        place.showNothing();
        return;
    }
    if (column == 0) {
        cell.setId("search/card-" + std::to_string(row));  // named in "focus" test events
        place.showCard(model_.rows()[row], context_);
    } else if (const core::Manga* manga = mangaAt(index)) {
        cell.setId("search/cover-" + std::to_string(row) + "-" + std::to_string(column - 1));
        place.showCover(*manga, context_);
    } else {
        cell.setId("search/empty-" + std::to_string(index));
        place.showNothing();
    }
}

void GlobalSearchActivity::unbind(GridCell& cell) {
    static_cast<SearchCell&>(cell).clear();
}

void GlobalSearchActivity::select(std::size_t index) {
    const std::size_t row = index / columns();
    if (row >= model_.rows().size()) {
        return;
    }
    const net::CancelToken alive = alive_;
    auto changed = [this, alive](const core::Manga& now) {
        if (!alive.cancelled()) {
            libraryChanged(now);
        }
    };
    if (index % columns() == 0) {
        // The card: that source's screen, searching the same text (with Retry if it failed here).
        brls::Application::pushActivity(new SourceActivity(context_, model_.rows()[row].source, changed, model_.query()));
        return;
    }
    if (const core::Manga* manga = mangaAt(index)) {
        brls::Application::pushActivity(new MangaActivity(context_, *manga, {}, {}, changed));
    }
}

void GlobalSearchActivity::libraryChanged(const core::Manga& now) {
    model_.setInLibrary(now.id, now.inLibrary);  // the "In library" tag
    if (onLibraryChanged_) {
        onLibraryChanged_(now);
    }
}

void GlobalSearchActivity::openCoverMenu(std::size_t index) {
    const core::Manga* found = mangaAt(index);
    if (found == nullptr) {
        return;  // a card: nothing to add
    }
    const core::Manga manga = *found;
    const std::vector<std::string> entries = {"browse/open"_i18n,
                                              manga.inLibrary ? "browse/cover_remove"_i18n : "browse/cover_add"_i18n};
    const net::CancelToken alive = alive_;
    auto* dropdown = new brls::Dropdown(manga.title, entries, [this, alive, manga, index](int selected) {
        // The menu closes first; what comes next opens on the following frame.
        brls::sync([this, alive, manga, index, selected] {
            if (alive.cancelled()) {
                return;
            }
            if (selected == 0) {
                select(index);
            } else if (selected == 1) {
                if (manga.inLibrary) {
                    flow_->removeFromLibrary(manga);
                } else {
                    flow_->add(manga);
                }
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace snx::ui
