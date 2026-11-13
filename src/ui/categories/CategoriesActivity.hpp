#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "app/AppContext.hpp"
#include "ui/categories/CategoriesModel.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"

// The Categories screen (WP-24, D54): the user's categories as a list (name, "3 manga"), Add on top.
// Controller: A on a row opens its menu (Rename..., Move up, Move down, Delete...), Y adds, L / R move the row up / down,
// B goes back. Touch: tap a row for its menu, tap Add. Names come from the keyboard (askText). The state lives in
// CategoriesModel. Layout: resources/xml/activity/categories.xml.
namespace snx::ui {

class CategoriesActivity : public brls::Activity, private GridSource {
  public:
    // onClosed(changed): when the screen goes; changed = something was saved (the Library loads everything again).
    CategoriesActivity(app::AppContext& context, std::function<void(bool)> onClosed);
    ~CategoriesActivity() override;

    CONTENT_FROM_XML_RES("activity/categories.xml");
    void onContentAvailable() override;

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
    void openRowMenu(std::size_t index);
    void askAdd();
    void askRename(std::size_t index);
    void askDelete(std::size_t index);
    void move(int step);
    void problem(CategoriesModel::Action action, core::NameProblem problem, const std::string& name);
    void report();

    BRLS_BIND(brls::Button, addButton, "categories/add");
    BRLS_BIND(brls::Box, body, "categories/body");

    app::AppContext& context_;
    CategoriesModel model_;
    std::function<void(bool)> onClosed_;
    StatusView* status_ = nullptr;
    CoverGrid* list_ = nullptr;
    bool ready_ = false;
    bool listShown_ = false;
    std::uint64_t shownRevision_ = 0;
    std::string reported_;
};

}  // namespace snx::ui
