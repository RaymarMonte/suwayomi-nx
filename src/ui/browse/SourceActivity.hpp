#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "app/AppContext.hpp"
#include "core/models/Source.hpp"
#include "net/CancelToken.hpp"
#include "ui/browse/SourceBrowseModel.hpp"
#include "ui/browse/SourceFiltersModel.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"

// A source's screen (WP-14, D56): Popular | Latest (Latest only when the source has it), Search, Filters ("Filters (2)") and
// Refresh above the source's cover grid, with an "In library" tag on covers already in the library. A search text and / or
// changed filters search the source: the bar shows the search and Clear in place of the modes.
// Controller: L / R Popular / Latest, A opens the preview (MangaActivity), Y the cover's menu (Open, Add to library... /
// Remove from library...), X refresh (or, after "Couldn't load more", that page again), MINUS the menu (Popular, Latest,
// Search..., Filters..., Refresh), B clears a search, else goes back. Touch: tap a cover or a button (D16).
// Paging as the library (D42): the next page when the grid shows the last half page. The state: SourceBrowseModel; the
// filters (loaded when the screen opens): SourceFiltersModel, edited on FiltersActivity. Layout: activity/source.xml.
namespace snx::ui {

class AddFlow;

class SourceActivity : public brls::Activity, private GridSource {
  public:
    // onLibraryChanged: a manga was added or removed here or on a preview opened from here (the library updates).
    SourceActivity(app::AppContext& context, core::Source source, std::function<void(const core::Manga&)> onLibraryChanged);
    ~SourceActivity() override;

    CONTENT_FROM_XML_RES("activity/source.xml");
    void onContentAvailable() override;
    void onResume() override;

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
    void updateBar();
    void setMode(SourceBrowseModel::Mode mode);
    void askSearch();
    void openFilters();
    void applyFilters();  // the filters screen's Apply: search with them
    void openMenu();
    void openCoverMenu(std::size_t index);
    void refresh();
    void back();
    void libraryChanged(const core::Manga& now);
    void report();
    void reportScreen();

    BRLS_BIND(brls::Box, modes, "source/modes");
    BRLS_BIND(brls::Button, popularButton, "source/popular");
    BRLS_BIND(brls::Button, latestButton, "source/latest");
    BRLS_BIND(brls::Box, searchBox, "source/searchBox");
    BRLS_BIND(brls::Label, searchLabel, "source/searchLabel");
    BRLS_BIND(brls::Button, clearButton, "source/clear");
    BRLS_BIND(brls::Button, searchButton, "source/search");
    BRLS_BIND(brls::Button, filtersButton, "source/filters");
    BRLS_BIND(brls::Button, refreshButton, "source/refresh");
    BRLS_BIND(brls::Box, body, "source/body");

    app::AppContext& context_;
    SourceBrowseModel model_;
    SourceFiltersModel filters_;
    std::function<void(const core::Manga&)> onLibraryChanged_;
    std::shared_ptr<AddFlow> flow_;
    net::CancelToken alive_;     // cancelled when this screen goes
    net::CancelToken prefetch_;  // the row below the screen
    StatusView* status_ = nullptr;
    CoverGrid* grid_ = nullptr;
    bool ready_ = false;
    bool gridShown_ = false;
    bool searchShown_ = false;
    std::uint64_t shownList_ = 0;
    std::uint64_t shownItems_ = 0;
    std::size_t shownCount_ = 0;
    bool moreFailedShown_ = false;
    std::int64_t startedAtMs_ = 0;  // the first page asked (the app's clock), for the event's "ms"
    std::string reported_;
};

}  // namespace snx::ui
