#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "core/models/Manga.hpp"
#include "ui/categories/CategoryPickerModel.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"

// The category picker (WP-24, D54): "Categories for <title>", a checklist of the user's categories (none ticked =
// Default). Controller: A ticks / unticks, X saves, Y makes a new category (ticked), B leaves without saving. Touch: tap a
// row, Save, New category. The state lives in CategoryPickerModel. Layout: resources/xml/activity/category_picker.xml.
// WP-14 can open it to choose the categories of a manga it adds (see CategoryPickerModel).
namespace snx::ui {

class CategoryPickerActivity : public brls::Activity, private GridSource {
  public:
    // current: the manga's categories when known (the details screen), else they are asked from the server.
    // onSaved: after a save that changed something, with the manga's categories now. The screen closes after Save.
    CategoryPickerActivity(app::AppContext& context, core::Manga manga,
                           std::optional<std::vector<core::Category>> current,
                           std::function<void(int mangaId, const std::vector<core::Category>&)> onSaved);
    ~CategoryPickerActivity() override;

    CONTENT_FROM_XML_RES("activity/category_picker.xml");
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
    void save();
    void askNew();
    void report();

    BRLS_BIND(brls::Button, newButton, "picker/new");
    BRLS_BIND(brls::Button, saveButton, "picker/save");
    BRLS_BIND(brls::Box, body, "picker/body");

    app::AppContext& context_;
    core::Manga manga_;
    CategoryPickerModel model_;
    std::function<void(int, const std::vector<core::Category>&)> onSaved_;
    StatusView* status_ = nullptr;
    CoverGrid* list_ = nullptr;
    bool ready_ = false;
    bool listShown_ = false;
    bool closing_ = false;
    std::uint64_t shownRevision_ = 0;
    std::size_t shownRows_ = 0;
    std::string reported_;
};

}  // namespace snx::ui
