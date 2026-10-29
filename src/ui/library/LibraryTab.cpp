#include "ui/library/LibraryTab.hpp"

#include <algorithm>
#include <utility>

#include "app/TestHooks.hpp"
#include "image/Caches.hpp"
#include "ui/components/TextFit.hpp"
#include "ui/manga/MangaActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// The grid's cells. 6 columns fit the tab (1280 - 410 px sidebar - 2 x 24 padding); a cell is far over the 72 px touch minimum.
constexpr float kCoverWidth = 120;
constexpr float kCoverHeight = 180;  // covers are 2:3
constexpr float kTitleGap = 6;
constexpr float kTitleHeight = 44;   // two lines
constexpr float kTitleFontSize = 16;
constexpr float kTitleLineHeight = 1.25f;
constexpr GridMetrics kMetrics{kCoverWidth, kCoverHeight + kTitleGap + kTitleHeight, 16, 24};

const NVGcolor kPlaceholder = nvgRGB(60, 60, 64);
const NVGcolor kBadge = nvgRGB(0, 150, 200);

const char* const kSortKeys[] = {"library/sort_title", "library/sort_unread", "library/sort_last_read"};
const core::LibrarySort kSorts[] = {core::LibrarySort::Title, core::LibrarySort::Unread, core::LibrarySort::LastRead};

std::string sortLabel(core::LibrarySort sort) {
    return brls::getStr(kSortKeys[static_cast<int>(sort)]);
}

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

// One cover: the image (a grey placeholder until it arrives), the unread badge on it, the title below.
class CoverCell : public GridCell {
  public:
    CoverCell() {
        setAxis(brls::Axis::COLUMN);
        setCornerRadius(4);
        setHighlightCornerRadius(6);

        cover_ = new brls::Box();
        cover_->setWidth(kCoverWidth);
        cover_->setHeight(kCoverHeight);
        cover_->setBackgroundColor(kPlaceholder);
        cover_->setCornerRadius(4);
        cover_->setClipsToBounds(true);
        image_ = new brls::Image();
        image_->setWidth(kCoverWidth);
        image_->setHeight(kCoverHeight);
        image_->setScalingType(brls::ImageScalingType::FILL);
        image_->setFreeTexture(true);
        cover_->addView(image_);

        badgeBox_ = new brls::Box();
        badgeBox_->detach();  // on top of the cover, top left
        badgeBox_->setDetachedPosition(6, 6);
        badgeBox_->setBackgroundColor(kBadge);
        badgeBox_->setCornerRadius(4);
        badgeBox_->setPadding(2, 6, 2, 6);
        badge_ = new brls::Label();
        badge_->setFontSize(14);
        badge_->setTextColor(nvgRGB(255, 255, 255));
        badge_->setAutoAnimate(false);
        badgeBox_->addView(badge_);
        cover_->addView(badgeBox_);
        addView(cover_);

        title_ = new brls::Label();
        title_->setWidth(kCoverWidth);
        title_->setHeight(kTitleHeight);
        title_->setMarginTop(kTitleGap);
        title_->setFontSize(kTitleFontSize);
        title_->setLineHeight(kTitleLineHeight);
        title_->setIsWrapping(true);
        title_->setAutoAnimate(false);
        addView(title_);
    }

    ~CoverCell() override { cancel_.cancel(); }

    void show(const core::Manga& manga, std::string url, app::AppContext& context) {
        title_->setText(fitLines(manga.title, kCoverWidth, title_->getFont(), kTitleFontSize, kTitleLineHeight, 2).text);
        if (manga.unreadCount > 0) {
            badge_->setText(std::to_string(manga.unreadCount));
            badgeBox_->setVisibility(brls::Visibility::VISIBLE);
        } else {
            badgeBox_->setVisibility(brls::Visibility::GONE);
        }
        badgeBox_->invalidate();  // detached: lays itself out
        if (url.empty()) {
            return;
        }
        const image::LoadRequest request{std::move(url), coverBox(), net::Priority::Interactive, image::ImageKind::Cover};
        if (image::ImagePtr ready = context.images.cached(request.url, request.box, image::ImageKind::Cover)) {
            setCover(ready, context.textures);
            return;
        }
        cancel_ = net::CancelToken();
        const net::CancelToken token = cancel_;
        image::TextureBudget* textures = &context.textures;
        context.images.load(request, token, [this, token, textures](const image::ImageResult& result) {
            if (token.cancelled()) {
                return;  // scrolled away, or the cell is gone
            }
            if (result.ok()) {
                setCover(result.value(), *textures);
            }
        });
    }

    void clear() {
        cancel_.cancel();
        image_->clear();
        lease_.reset();
    }

    // The size to decode covers at: the cover in screen pixels.
    static core::ImageSize coverBox() {
        const float scale = std::max(1.0f, brls::Application::windowScale);
        return {static_cast<int>(kCoverWidth * scale), static_cast<int>(kCoverHeight * scale)};
    }

  private:
    void setCover(const image::ImagePtr& decoded, image::TextureBudget& textures) {
        if (!decoded || decoded->tiles().empty()) {
            return;
        }
        // A cover decoded for this box is one tile (far below maxTextureSide).
        const core::TileRect& tile = decoded->tiles()[0];
        image::TextureLease lease(textures, image::TextureBudget::bytesFor(tile));
        if (!lease.ok()) {
            return;  // the GPU budget is full (S7): keep the placeholder
        }
        std::vector<std::uint8_t> scratch;
        const std::uint8_t* pixels = decoded->tilePixels(0, scratch);
        const int texture =
            pixels ? nvgCreateImageRGBA(brls::Application::getNVGContext(), tile.width, tile.height, 0, pixels) : 0;
        if (texture == 0) {
            return;
        }
        image_->clear();
        lease_ = std::move(lease);
        image_->innerSetImage(texture);
    }

    brls::Box* cover_;
    brls::Image* image_;
    brls::Box* badgeBox_;
    brls::Label* badge_;
    brls::Label* title_;
    net::CancelToken cancel_;
    image::TextureLease lease_;
};

}  // namespace

LibraryTab::LibraryTab(app::AppContext& context) : context_(context) {
    inflateFromXMLRes("xml/tabs/library.xml");
    setDefaultFocusedIndex(1);  // coming from the tab list, the focus lands on the covers, not on the category bar
    status_ = new StatusView();
    status_->setGrow(1.0f);
    body->addView(status_);
    grid_ = new CoverGrid(kMetrics);
    grid_->setGrow(1.0f);
    grid_->setVisibility(brls::Visibility::GONE);
    body->addView(grid_);

    sortButton->registerClickAction([this](brls::View*) {
        openSortMenu();
        return true;
    });
    refreshButton->registerClickAction([this](brls::View*) {
        refresh();
        return true;
    });
    registerAction("library/category"_i18n, brls::BUTTON_RB, [this](brls::View*) {
        if (model_ != nullptr) {
            model_->stepCategory(1);
        }
        return true;
    });
    registerAction("library/category"_i18n, brls::BUTTON_LB, [this](brls::View*) {
        if (model_ != nullptr) {
            model_->stepCategory(-1);
        }
        return true;
    }, true);
    registerAction("library/sort"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        openSortMenu();
        return true;
    });
    registerAction("library/refresh"_i18n, brls::BUTTON_X, [this](brls::View*) {
        refresh();
        return true;
    });
    // PLUS quits the app everywhere (the device scripts rely on it), so the menu is on MINUS (owner OK, D42).
    registerAction("library/menu"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        openMenu();
        return true;
    });
}

LibraryTab::~LibraryTab() {
    prefetch_.cancel();
    grid_->setSource(nullptr);  // unbinds the cells (cancels their loads) while this tab still exists
    if (model_ != nullptr) {
        model_->setListener({});
    }
}

void LibraryTab::showStatusOnly() {
    attach(nullptr);
}

void LibraryTab::attach(LibraryModel* model) {
    if (model == model_) {
        return;
    }
    if (model_ != nullptr) {
        model_->setListener({});
    }
    grid_->setSource(nullptr);
    model_ = model;
    shownCategories_.clear();
    shownCategory_ = -1;
    shownCount_ = 0;
    reported_.clear();
    if (model_ == nullptr) {
        bar->setVisibility(brls::Visibility::GONE);
        showGrid(false);
        return;
    }
    model_->setListener([this] { modelChanged(); });
    grid_->setSource(this);
    modelChanged();
}

void LibraryTab::showGrid(bool visible) {
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
        // A hidden cell keeps no focus: the status takes it. Only now, once it is visible: a hidden view refuses the focus,
        // and the highlight would stay on the hidden cell (found on the console: R to an empty category).
        if (hadFocus) {
            brls::Application::giveFocus(status_);
        }
    }
}

void LibraryTab::modelChanged() {
    if (model_ == nullptr) {
        return;
    }
    const LibraryModel& model = *model_;

    std::vector<int> ids;
    for (const core::Category& category : model.categories()) {
        ids.push_back(category.id);
    }
    if (ids != shownCategories_) {
        shownCategories_ = ids;
        rebuildCategories();
    }
    markCurrentCategory();
    sortButton->setText(brls::getStr("library/sort_button", sortLabel(model.sort())));

    const core::Category* category = model.category();
    const int categoryId = category != nullptr ? category->id : -1;
    switch (model.state()) {
        case LibraryModel::State::Loading:
            if (model.loadedCount() > 0 && model.sort() != core::LibrarySort::Title) {
                status_->showLoading(brls::getStr("library/loading_all", model.loadedCount(), model.totalCount()));
            } else {
                status_->showLoading("library/loading"_i18n);
            }
            showGrid(false);
            break;
        case LibraryModel::State::Failed:
            status_->showError("Library", model.error(), [this] { refresh(); });
            showGrid(false);
            break;
        case LibraryModel::State::Empty:
            if (model.libraryEmpty()) {
                status_->showMessage("library/empty_title"_i18n, "library/empty_text"_i18n);
            } else {
                status_->showMessage(brls::getStr("library/category_empty", category ? category->name : ""),
                                     "library/category_empty_text"_i18n);
            }
            showGrid(false);
            break;
        case LibraryModel::State::Ready: {
            const std::size_t count = model.items().size();
            const bool same = shownState_ == LibraryModel::State::Ready && categoryId == shownCategory_ &&
                              model.sort() == shownSort_ && count >= shownCount_ && model.revision() == shownRevision_;
            showGrid(true);
            if (same && count > shownCount_) {
                grid_->appended();
            } else if (!same) {
                grid_->reload(model.rememberedFocus());
            }
            shownCount_ = count;
            shownRevision_ = model.revision();
            break;
        }
    }
    if (model.moreFailed() && !moreFailedShown_) {
        brls::Application::notify("library/more_failed"_i18n);
    }
    moreFailedShown_ = model.moreFailed();
    shownState_ = model.state();
    shownCategory_ = categoryId;
    shownSort_ = model.sort();
    if (model.state() != LibraryModel::State::Ready) {
        shownCount_ = 0;
    }

    const core::LibrarySettings& saved = context_.config.config().library;
    if (category != nullptr && (saved.categoryId != model.settings().categoryId || saved.sort != model.settings().sort)) {
        context_.config.setLibrary(model.settings());
    }
    report();
}

void LibraryTab::report() {
    const LibraryModel& model = *model_;
    if (model.state() == LibraryModel::State::Loading) {
        reported_.clear();  // after a refresh the same result is reported again
        return;
    }
    static const char* const kStates[] = {"loading", "ready", "empty", "failed"};
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = kStates[static_cast<int>(model.state())];
    fields["category"] = model.category() != nullptr ? model.category()->id : -1;
    fields["categories"] = model.categories().size();
    fields["sort"] = std::string(core::librarySortName(model.sort()));
    fields["items"] = model.items().size();
    fields["total"] = model.totalCount();
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        app::testhooks::write("library", fields);
    }
}

void LibraryTab::rebuildCategories() {
    // Never delete the focused view: park the focus on Sort while the buttons are rebuilt.
    const bool hadFocus = inside(brls::Application::getCurrentFocus(), categoryBox);
    if (hadFocus) {
        brls::Application::giveFocus(sortButton);
    }
    categoryBox->clearViews();
    categoryButtons_.clear();
    const auto& categories = model_->categories();
    for (std::size_t i = 0; i < categories.size(); ++i) {
        auto* button = new brls::Button();
        button->setText(categories[i].name);
        button->setHeight(72);
        button->setPaddingLeft(14);
        button->setPaddingRight(14);
        button->setMarginRight(8);
        button->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        button->registerClickAction([this, i](brls::View*) {
            if (model_ != nullptr) {
                model_->selectCategory(i);
            }
            return true;
        });
        button->addGestureRecognizer(new brls::TapGestureRecognizer(button));
        categoryBox->addView(button);
        categoryButtons_.push_back(button);
    }
    bar->setVisibility(categories.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    markCurrentCategory();
    if (hadFocus && !categoryButtons_.empty()) {
        brls::Application::giveFocus(categoryButtons_[std::min(model_->categoryIndex(), categoryButtons_.size() - 1)]);
    }
}

void LibraryTab::markCurrentCategory() {
    for (std::size_t i = 0; i < categoryButtons_.size(); ++i) {
        const bool current = i == model_->categoryIndex();
        categoryButtons_[i]->setStyle(current ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_BORDERLESS);
    }
}

std::size_t LibraryTab::count() const {
    return model_ != nullptr && model_->state() == LibraryModel::State::Ready ? model_->items().size() : 0;
}

GridCell* LibraryTab::makeCell() {
    return new CoverCell();
}

void LibraryTab::bind(GridCell& cell, std::size_t index) {
    const core::Manga& manga = model_->items()[index];
    cell.setId("library/cover-" + std::to_string(index));  // the "focus" test event then says which cover has the focus
    const std::string url = manga.thumbnailUrl.empty() ? std::string() : context_.client.absoluteUrl(manga.thumbnailUrl);
    static_cast<CoverCell&>(cell).show(manga, url, context_);
}

void LibraryTab::unbind(GridCell& cell) {
    static_cast<CoverCell&>(cell).clear();
}

void LibraryTab::select(std::size_t index) {
    if (model_ == nullptr || index >= model_->items().size()) {
        return;
    }
    model_->rememberFocus(index);
    // Chapter actions change the unread count: the badge (and the Unread sort) follow.
    const std::weak_ptr<int> alive = alive_;
    brls::Application::pushActivity(new MangaActivity(context_, model_->items()[index], [this, alive](const core::Manga& manga) {
        if (!alive.expired() && model_ != nullptr) {
            model_->updateManga(manga);
        }
    }));
}

void LibraryTab::focused(std::size_t index) {
    if (model_ != nullptr) {
        model_->rememberFocus(index);
    }
}

void LibraryTab::showing(std::size_t first, std::size_t last) {
    if (model_ == nullptr || last == 0) {
        return;
    }
    // For the tests: which items have cells now (cover loads for others would be wasted, WP-09 acceptance).
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["first"] = first;
    fields["last"] = last;
    fields["columns"] = GridLayout(grid_->getWidth(), kMetrics).columns();
    app::testhooks::write("grid", fields);
    model_->showing(last - 1);  // near the end of what is loaded: the next page
    prefetchCovers(first, last);
}

void LibraryTab::prefetchCovers(std::size_t first, std::size_t last) {
    (void)first;
    prefetch_.cancel();
    prefetch_ = net::CancelToken();
    if (!context_.profile.coverRowPrefetch) {
        return;
    }
    // The row below the screen, so scrolling down finds its covers ready (one row: the grid's width in cells).
    const std::size_t columns = static_cast<std::size_t>(GridLayout(grid_->getWidth(), kMetrics).columns());
    const auto& items = model_->items();
    for (std::size_t i = last; i < std::min(items.size(), last + columns); ++i) {
        if (items[i].thumbnailUrl.empty()) {
            continue;
        }
        image::LoadRequest request{context_.client.absoluteUrl(items[i].thumbnailUrl), CoverCell::coverBox(),
                                   net::Priority::Prefetch, image::ImageKind::Cover};
        context_.images.load(std::move(request), prefetch_, [](const image::ImageResult&) {});
    }
}

void LibraryTab::refresh() {
    if (model_ != nullptr) {
        model_->refresh();
    }
}

void LibraryTab::openSortMenu() {
    if (model_ == nullptr) {
        return;
    }
    std::vector<std::string> names;
    for (const core::LibrarySort sort : kSorts) {
        names.push_back(sortLabel(sort));
    }
    auto* dropdown = new brls::Dropdown(
        "library/sort"_i18n, names,
        [this](int selected) {
            if (model_ != nullptr && selected >= 0 && selected < 3) {
                model_->setSort(kSorts[selected]);
            }
        },
        static_cast<int>(model_->sort()));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibraryTab::openCategoryMenu() {
    if (model_ == nullptr || model_->categories().empty()) {
        return;
    }
    std::vector<std::string> names;
    for (const core::Category& category : model_->categories()) {
        names.push_back(category.name);
    }
    auto* dropdown = new brls::Dropdown(
        "library/category"_i18n, names,
        [this](int selected) {
            if (model_ != nullptr && selected >= 0) {
                model_->selectCategory(static_cast<std::size_t>(selected));
            }
        },
        static_cast<int>(model_->categoryIndex()));
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void LibraryTab::openMenu() {
    if (model_ == nullptr) {
        return;
    }
    const std::vector<std::string> entries = {"library/menu_category"_i18n, "library/menu_sort"_i18n,
                                              "library/menu_refresh"_i18n};
    auto* dropdown = new brls::Dropdown("library/menu_title"_i18n, entries, [this](int selected) {
        // The menu closes first; the next one opens on the following frame.
        brls::sync([this, selected] {
            if (selected == 0) {
                openCategoryMenu();
            } else if (selected == 1) {
                openSortMenu();
            } else if (selected == 2) {
                refresh();
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace snx::ui
