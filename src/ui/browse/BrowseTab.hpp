#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "app/AppContext.hpp"
#include "core/models/Manga.hpp"
#include "ui/browse/SourcesModel.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"

// The Browse tab (WP-14, D56): the server's sources, one per 80 px row: the language label on each language's first row,
// the source's icon, its name and an "18+" tag (contentWarning NSFW). Order: Last used, Local, then languages and sources
// by name (core SourceLanguages). Controller: A opens the source (SourceActivity; it becomes Last used), X refresh, B back
// to the tab list. Touch: tap a row, Refresh. Only Local installed: the bar says how to get sources.
// The state lives in SourcesModel, which MainActivity owns (the tab frame deletes this view on a tab change, D42).
// onLibraryChanged: a manga was added to or removed from the library on a screen opened from here (MainActivity tells the
// library). Layout: resources/xml/tabs/browse.xml.
namespace snx::ui {

class BrowseTab : public brls::Box, private GridSource {
  public:
    BrowseTab(app::AppContext& context, std::function<void(const core::Manga&)> onLibraryChanged);
    ~BrowseTab() override;

    // Not connected yet: the status, which MainActivity fills.
    StatusView& status() { return *status_; }
    // nullptr: only the status.
    void attach(SourcesModel* model);

  private:
    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;
    void focused(std::size_t index) override;

    void modelChanged();
    void showList(bool visible);
    void refresh();
    void report();

    BRLS_BIND(brls::Label, note, "browse/note");
    BRLS_BIND(brls::Button, refreshButton, "browse/refresh");
    BRLS_BIND(brls::Box, body, "browse/body");

    app::AppContext& context_;
    std::function<void(const core::Manga&)> onLibraryChanged_;
    SourcesModel* model_ = nullptr;
    StatusView* status_;
    CoverGrid* list_;
    bool listShown_ = false;
    std::uint64_t shownRevision_ = 0;
    std::string reported_;
};

}  // namespace snx::ui
