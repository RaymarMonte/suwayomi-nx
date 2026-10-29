#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "app/AppContext.hpp"
#include "core/models/Manga.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/CoverImage.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/manga/MangaModel.hpp"

// A manga's details screen (WP-10, D43). Left: cover, title, author, status, genres, Continue, and the description (A or a
// tap opens and closes a long one). Right: the chapter count, Sort, Show (the filter), Refresh, and the chapter list (a
// recycling CoverGrid with one column) or a status (loading, error + Retry, no chapters).
// Controller: A on a chapter opens it, X marks it read / unread, Y opens its menu, MINUS the screen menu, B goes back.
// Touch: tap a chapter to open it, its "..." for its menu, and the buttons for everything else (D16).
// The state lives in MangaModel. Opening a chapter starts the reader (a stand-in until WP-11). Layout:
// resources/xml/activity/manga.xml.
namespace snx::ui {

class MangaActivity : public brls::Activity, private GridSource {
  public:
    // onChanged: the manga's unread count changed (chapter actions); the library updates its badge. May be empty.
    MangaActivity(app::AppContext& context, core::Manga manga, std::function<void(const core::Manga&)> onChanged);
    ~MangaActivity() override;

    CONTENT_FROM_XML_RES("activity/manga.xml");
    void onContentAvailable() override;
    void onResume() override;  // back on top (from the reader or a menu): the "screen" test event again

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
    void showDetails();
    void showDescription();
    void updateBar();
    void updateContinue();
    void showList(bool visible);
    void toggleDescription();
    void continueReading();
    void openChapter(const core::Chapter& chapter, int page);
    void openChapterMenu(std::size_t index);
    void openSortMenu();
    void openFilterMenu();
    void openMenu();
    void act(int chapterId, MangaModel::Action action);
    void refresh();
    void report();
    void reportScreen();

    BRLS_BIND(brls::Box, header, "manga/header");
    BRLS_BIND(brls::Label, title, "manga/title");
    BRLS_BIND(brls::Label, author, "manga/author");
    BRLS_BIND(brls::Label, status, "manga/status");
    BRLS_BIND(brls::Label, genres, "manga/genres");
    BRLS_BIND(brls::Button, continueButton, "manga/continue");
    BRLS_BIND(brls::Box, descriptionBox, "manga/descriptionBox");
    BRLS_BIND(brls::Label, description, "manga/description");
    BRLS_BIND(brls::Label, more, "manga/more");
    BRLS_BIND(brls::Label, countLabel, "manga/count");
    BRLS_BIND(brls::Button, sortButton, "manga/sort");
    BRLS_BIND(brls::Button, filterButton, "manga/filter");
    BRLS_BIND(brls::Button, refreshButton, "manga/refresh");
    BRLS_BIND(brls::Box, body, "manga/body");

    app::AppContext& context_;
    MangaModel model_;
    std::function<void(const core::Manga&)> onChanged_;
    CoverImage* cover_ = nullptr;
    StatusView* statusView_ = nullptr;
    CoverGrid* list_ = nullptr;
    bool ready_ = false;  // onContentAvailable ran: the views exist

    // What the screen shows now, to tell a change from more of the same.
    std::string detailsShown_;
    bool expanded_ = false;
    bool descriptionCut_ = false;
    bool listShown_ = false;
    std::uint64_t shownList_ = 0;
    std::uint64_t shownItems_ = 0;
    std::size_t shownCount_ = 0;
    bool moreFailedShown_ = false;
    int reportedUnread_;
    std::string pendingAction_;  // the action under way, for its "chapter_action" test event
    int pendingChapter_ = 0;
    std::string reported_;
};

}  // namespace snx::ui
