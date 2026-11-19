#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "ui/browse/LanguagesModel.hpp"
#include "ui/components/CoverGrid.hpp"

// The Languages checklist (WP-14b, D56), from the Browse tab: every language that has a source (Local included), ticked =
// listed on the Browse tab and searched by Search all. At least one stays ticked. Controller: A ticks / unticks, X Done
// (saves), B leaves without saving. Touch: tap a row, Done. The rules live in LanguagesModel. Layout:
// resources/xml/activity/languages.xml (the category picker's).
namespace snx::ui {

class LanguagesActivity : public brls::Activity, private GridSource {
  public:
    // languages: SourcesModel::languages(). onDone: Done with a change, with the codes to hide now (the caller saves them).
    LanguagesActivity(app::AppContext& context, const std::vector<std::string>& languages,
                      std::vector<std::string> hidden, std::function<void(std::vector<std::string> hidden)> onDone);
    ~LanguagesActivity() override;

    CONTENT_FROM_XML_RES("activity/languages.xml");
    void onContentAvailable() override;

  private:
    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;

    void done();
    void report(const char* state);

    BRLS_BIND(brls::Button, doneButton, "languages/done");
    BRLS_BIND(brls::Box, body, "languages/body");

    LanguagesModel model_;
    std::function<void(std::vector<std::string>)> onDone_;
    CoverGrid* list_ = nullptr;
    bool closing_ = false;
};

}  // namespace snx::ui
