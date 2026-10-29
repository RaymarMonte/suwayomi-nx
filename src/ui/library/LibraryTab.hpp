#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "net/CancelToken.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/library/LibraryModel.hpp"

// The Library tab (WP-09): the category bar (a tab per category, Sort, Refresh) above a grid of covers with titles and
// unread badges, or a status (loading, error + Retry, empty). Controller: L/R category, Y sort, X refresh, MINUS a menu,
// A opens the manga. Touch: tap a category / a cover / Sort / Refresh, drag and flick scroll (D16).
// The state lives in LibraryModel, which MainActivity owns: the tab frame deletes this view whenever another tab opens, and
// the next one picks up at the same category and cover. Covers load at Interactive priority for the cells on screen and are
// cancelled when a cell scrolls away; with RuntimeProfile::coverRowPrefetch the row below loads at Prefetch priority.
// Layout: resources/xml/tabs/library.xml.
namespace snx::ui {

class LibraryTab : public brls::Box, private GridSource {
  public:
    explicit LibraryTab(app::AppContext& context);
    ~LibraryTab() override;

    // Before the library can load (not set up, connecting, the connection failed): the status, which MainActivity fills.
    StatusView& status() { return *status_; }
    void showStatusOnly();
    // The server answered: show this model (MainActivity owns it and starts it). nullptr: back to showStatusOnly().
    void attach(LibraryModel* model);

  private:
    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;
    void focused(std::size_t index) override;
    void showing(std::size_t first, std::size_t last) override;

    void modelChanged();
    void showGrid(bool visible);
    void rebuildCategories();
    void markCurrentCategory();
    void prefetchCovers(std::size_t first, std::size_t last);
    void openSortMenu();
    void openCategoryMenu();
    void openMenu();
    void refresh();
    void report();  // the "library" test event when what the tab shows changed

    BRLS_BIND(brls::Box, bar, "library/bar");
    BRLS_BIND(brls::Box, categoryBox, "library/categories");
    BRLS_BIND(brls::Button, sortButton, "library/sort");
    BRLS_BIND(brls::Button, refreshButton, "library/refresh");
    BRLS_BIND(brls::Box, body, "library/body");

    app::AppContext& context_;
    LibraryModel* model_ = nullptr;
    StatusView* status_;
    CoverGrid* grid_;
    std::vector<brls::Button*> categoryButtons_;
    net::CancelToken prefetch_;  // the row below the screen
    // The manga screen tells the library about changes through this tab while it exists (weak_ptr: it may be gone).
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    // What the tab shows now, to tell a change from more of the same.
    std::vector<int> shownCategories_;
    int shownCategory_ = -1;
    core::LibrarySort shownSort_ = core::LibrarySort::Title;
    LibraryModel::State shownState_ = LibraryModel::State::Loading;
    std::size_t shownCount_ = 0;
    std::uint64_t shownRevision_ = 0;
    bool gridShown_ = false;
    bool moreFailedShown_ = false;
    std::string reported_;
};

}  // namespace snx::ui
