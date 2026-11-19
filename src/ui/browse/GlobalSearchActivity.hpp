#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "core/models/Manga.hpp"
#include "core/models/Source.hpp"
#include "net/CancelToken.hpp"
#include "ui/browse/GlobalSearchModel.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"

// Search all (WP-14b, D56): one text in every shown source (GlobalSearchModel), a row per source: its card (icon, name,
// Waiting / Searching... / 3 found / Nothing found / Failed: why) and its first covers (globalSearchCovers, 5), in a grid
// of exactly 1 + 5 = 6 columns whose cell width comes from the screen width. Empty places can't take the focus (a move
// into one lands on the row's last cover, or its card). Rows keep their places while the answers come in.
// Controller: A on a card opens that source's screen searching the text (Nothing found and Failed rows too: Retry is there),
// A on a cover the preview (MangaActivity), Y on a cover its menu (Open, Add to library... / Remove from library...),
// X a new text, B back (what still runs is cancelled, waiting sources never start). Touch: tap a card, a cover, Search.
// Layout: resources/xml/activity/global_search.xml.
namespace snx::ui {

class AddFlow;

class GlobalSearchActivity : public brls::Activity, private GridSource {
  public:
    // sources / hiddenLanguages: the Browse tab's (SourcesModel). query: the text already asked (not empty).
    // onLibraryChanged: a manga was added or removed here or on a screen opened from here (the library updates).
    GlobalSearchActivity(app::AppContext& context, const std::vector<core::Source>& sources,
                         const std::vector<std::string>& hiddenLanguages, std::string query,
                         std::function<void(const core::Manga&)> onLibraryChanged);
    ~GlobalSearchActivity() override;

    CONTENT_FROM_XML_RES("activity/global_search.xml");
    void onContentAvailable() override;
    void onResume() override;  // back from a screen opened here: says so (the "screen" test event)

  private:
    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;
    bool focusable(std::size_t index) const override;

    std::size_t columns() const;  // 1 card + globalSearchCovers covers
    const core::Manga* mangaAt(std::size_t index) const;  // nullptr for a card or an empty place

    void search(std::string query);
    void askSearch();
    void modelChanged();
    void openCoverMenu(std::size_t index);
    void libraryChanged(const core::Manga& now);
    void report();

    BRLS_BIND(brls::Box, root, "search/root");
    BRLS_BIND(brls::Label, progress, "search/progress");
    BRLS_BIND(brls::Button, searchButton, "search/search");
    BRLS_BIND(brls::Box, body, "search/body");

    app::AppContext& context_;
    GlobalSearchModel model_;
    std::string firstQuery_;
    std::function<void(const core::Manga&)> onLibraryChanged_;
    std::shared_ptr<AddFlow> flow_;
    net::CancelToken alive_;  // cancelled when this screen goes
    StatusView* status_ = nullptr;
    CoverGrid* grid_ = nullptr;
    bool ready_ = false;
    bool gridShown_ = false;
    bool startFocusMoved_ = false;  // the focus went from the bar (borealis's start) to the first card once
    std::uint64_t shownList_ = 0;
    std::uint64_t shownItems_ = 0;
    std::int64_t startedAtMs_ = 0;  // the text asked (the app's clock), for the event's "ms"
    std::string reported_;
};

}  // namespace snx::ui
