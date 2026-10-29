#pragma once

#include <borealis.hpp>

#include "app/AppContext.hpp"
#include "core/models/Manga.hpp"

// A manga's screen. For now (WP-09) a stand-in: its title and "details come later". WP-10 turns it into the details and
// chapter list. B goes back to the library.
namespace snx::ui {

class MangaActivity : public brls::Activity {
  public:
    MangaActivity(app::AppContext& context, core::Manga manga);

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    app::AppContext& context_;
    core::Manga manga_;
};

}  // namespace snx::ui
