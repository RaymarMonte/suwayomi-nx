#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "app/AppContext.hpp"
#include "ui/browse/SourceFiltersModel.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"

// A source's filters (WP-14, D56): one 72 px row per filter. Header: a note (A does nothing). Text "Author: any" (A types).
// Select "Status: Any" and Sort "Sort by: Popularity, descending" (A: a list; the current sort again flips its direction).
// CheckBox "In colour: on / off" (A toggles). TriState "Action: any / include / exclude" (A cycles). Group "Genres (1 set)"
// (A opens or closes it; its filters indented below; closed at first).
// Controller: X / Apply hands the values to the source screen, which searches with them; Y / Reset puts the defaults back;
// B leaves without changes. The model (SourceFiltersModel) belongs to the source screen, which is under this one while it is
// open. Layout: resources/xml/activity/filters.xml.
namespace snx::ui {

class FiltersActivity : public brls::Activity, private GridSource {
  public:
    // onApply: Apply was pressed (the model has the new values). onClosed: the screen is going (any way).
    FiltersActivity(app::AppContext& context, SourceFiltersModel& model, std::string sourceName, std::function<void()> onApply,
                    std::function<void()> onClosed);
    ~FiltersActivity() override;

    CONTENT_FROM_XML_RES("activity/filters.xml");
    void onContentAvailable() override;

  private:
    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;

    void modelChanged();
    void showList(bool visible);
    void apply();
    void reset();
    void report();

    BRLS_BIND(brls::Label, note, "filters/note");
    BRLS_BIND(brls::Button, resetButton, "filters/reset");
    BRLS_BIND(brls::Button, applyButton, "filters/apply");
    BRLS_BIND(brls::Box, body, "filters/body");

    app::AppContext& context_;
    SourceFiltersModel& model_;
    std::string sourceName_;
    std::function<void()> onApply_;
    std::function<void()> onClosed_;
    StatusView* status_ = nullptr;
    CoverGrid* list_ = nullptr;
    bool ready_ = false;
    bool listShown_ = false;
    bool closing_ = false;
    bool editing_ = false;  // beginEdit() ran (once the filters are in)
    std::uint64_t shownRevision_ = 0;
    std::size_t shownRows_ = 0;
    std::int64_t openedAtMs_ = 0;
    std::string reported_;
};

}  // namespace snx::ui
