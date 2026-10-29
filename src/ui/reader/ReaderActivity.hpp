#pragma once

#include <borealis.hpp>

#include "app/AppContext.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"

// The reader. For now (WP-10) a stand-in: which chapter it would open, at which page. WP-11 turns it into the paged
// reader (and then tells the details screen to reload its chapters: MangaModel::reloadChapters). B goes back.
namespace snx::ui {

class ReaderActivity : public brls::Activity {
  public:
    // page: the page index to open at (0 = the first page).
    ReaderActivity(app::AppContext& context, core::Manga manga, core::Chapter chapter, int page);

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    app::AppContext& context_;
    core::Manga manga_;
    core::Chapter chapter_;
    int page_;
};

}  // namespace snx::ui
