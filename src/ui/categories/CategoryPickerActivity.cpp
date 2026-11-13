#include "ui/categories/CategoryPickerActivity.hpp"

#include <utility>

#include "app/TestHooks.hpp"
#include "ui/components/AskText.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of rows, 80 px high (D16: over the 72 px touch minimum).
constexpr float kRowWidth = 1120;
constexpr float kRowHeight = 80;
constexpr GridMetrics kListMetrics{kRowWidth, kRowHeight, 4, 0};
constexpr float kBoxSide = 32;

NVGcolor themeColor(const char* name) {
    return brls::Application::getTheme()[name];
}

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

// One category with its tick box (drawn as boxes: no font needed).
class PickCell : public GridCell {
  public:
    PickCell() {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));
        setPaddingLeft(20);
        setPaddingRight(20);
        box_ = new brls::Box(brls::Axis::ROW);
        box_->setWidth(kBoxSide);
        box_->setHeight(kBoxSide);
        box_->setCornerRadius(4);
        box_->setBorderThickness(3);
        box_->setBorderColor(themeColor("brls/text"));
        box_->setJustifyContent(brls::JustifyContent::CENTER);
        box_->setAlignItems(brls::AlignItems::CENTER);
        tick_ = new brls::Box();
        tick_->setWidth(kBoxSide - 14);
        tick_->setHeight(kBoxSide - 14);
        tick_->setCornerRadius(2);
        box_->addView(tick_);
        addView(box_);
        name_ = new brls::Label();
        name_->setWidth(kRowWidth - 40 - kBoxSide - 20);
        name_->setMarginLeft(20);
        name_->setFontSize(20);
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        addView(name_);
    }

    void show(const CategoryPickerModel::Row& row) {
        name_->setText(row.category.name);
        tick_->setBackgroundColor(row.checked ? themeColor("brls/accent") : nvgRGBA(0, 0, 0, 0));
    }

  private:
    brls::Box* box_;
    brls::Box* tick_;
    brls::Label* name_;
};

}  // namespace

CategoryPickerActivity::CategoryPickerActivity(app::AppContext& context, core::Manga manga,
                                               std::optional<std::vector<core::Category>> current,
                                               std::function<void(int, const std::vector<core::Category>&)> onSaved)
    : context_(context),
      manga_(std::move(manga)),
      model_(context.client, manga_.id, std::move(current),
             static_cast<std::size_t>(context.profile.categoryNameMaxChars)),
      onSaved_(std::move(onSaved)) {}

CategoryPickerActivity::~CategoryPickerActivity() {
    model_.setListener({});
    if (list_ != nullptr) {
        list_->setSource(nullptr);
    }
}

void CategoryPickerActivity::onContentAvailable() {
    if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
        frame->setTitle(brls::getStr("categories/picker_title", manga_.title));
    }
    status_ = new StatusView();
    status_->setGrow(1.0f);
    body->addView(status_);
    list_ = new CoverGrid(kListMetrics);
    list_->setGrow(1.0f);
    list_->setVisibility(brls::Visibility::GONE);
    body->addView(list_);

    newButton->registerClickAction([this](brls::View*) {
        askNew();
        return true;
    });
    saveButton->registerClickAction([this](brls::View*) {
        save();
        return true;
    });
    getContentView()->registerAction("categories/save"_i18n, brls::BUTTON_X, [this](brls::View*) {
        save();
        return true;
    });
    getContentView()->registerAction("categories/new"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        askNew();
        return true;
    });

    ready_ = true;
    model_.setListener([this] { modelChanged(); });
    list_->setSource(this);
    model_.start();
    modelChanged();
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["id"] = manga_.id;
    app::testhooks::screen("CategoryPicker", params);
}

std::size_t CategoryPickerActivity::count() const {
    return model_.rows().size();
}

GridCell* CategoryPickerActivity::makeCell() {
    return new PickCell();
}

void CategoryPickerActivity::bind(GridCell& cell, std::size_t index) {
    if (index < model_.rows().size()) {
        static_cast<PickCell&>(cell).show(model_.rows()[index]);
    }
}

void CategoryPickerActivity::unbind(GridCell&) {}

void CategoryPickerActivity::select(std::size_t index) {
    model_.toggle(index);
}

void CategoryPickerActivity::showList(bool visible) {
    if (visible == listShown_) {
        return;
    }
    brls::View* focus = brls::Application::getCurrentFocus();
    const bool hadFocus = inside(focus, list_) || inside(focus, status_);
    listShown_ = visible;
    if (visible) {
        status_->setVisibility(brls::Visibility::GONE);
        list_->setVisibility(brls::Visibility::VISIBLE);
        if (hadFocus) {
            brls::Application::giveFocus(list_);
        }
    } else {
        list_->setVisibility(brls::Visibility::GONE);
        status_->setVisibility(brls::Visibility::VISIBLE);
        if (hadFocus) {
            brls::Application::giveFocus(status_);  // after the visibility switch (D42)
        }
    }
}

void CategoryPickerActivity::modelChanged() {
    if (!ready_ || closing_) {
        return;
    }
    switch (model_.state()) {
        case CategoryPickerModel::State::Loading:
            status_->showLoading("categories/picker_loading"_i18n);
            showList(false);
            break;
        case CategoryPickerModel::State::Failed:
            status_->showError("CategoryPicker", model_.error(), [this] { model_.retry(); });
            showList(false);
            break;
        case CategoryPickerModel::State::Ready:
        case CategoryPickerModel::State::Saving:
            if (model_.rows().empty()) {
                status_->showMessage("categories/title"_i18n, "categories/picker_none"_i18n);
                showList(false);
                break;
            }
            if (!listShown_) {
                showList(true);
                list_->reload(0);
            } else if (model_.rows().size() != shownRows_) {
                list_->reload(model_.rows().size() - 1);  // a new category: the focus goes to it
            } else if (model_.revision() != shownRevision_) {
                list_->rebind();
            }
            break;
        case CategoryPickerModel::State::Saved:
            break;
    }
    shownRevision_ = model_.revision();
    shownRows_ = listShown_ ? model_.rows().size() : 0;
    if (const std::optional<api::ApiError> failed = model_.takeActionError()) {
        brls::Application::notify(brls::getStr("categories/failed", errorText(*failed)));
    }
    report();
    if (model_.state() == CategoryPickerModel::State::Saved) {
        closing_ = true;
        if (model_.savedChange() && onSaved_) {
            onSaved_(manga_.id, model_.saved());
        }
        brls::sync([] { brls::Application::popActivity(); });
    }
}

void CategoryPickerActivity::report() {
    static const char* const kStates[] = {"loading", "ready", "failed", "saving", "saved"};
    if (model_.state() == CategoryPickerModel::State::Loading || model_.busy()) {
        return;
    }
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (const CategoryPickerModel::Row& row : model_.rows()) {
        rows.push_back({{"id", row.category.id}, {"name", row.category.name}, {"checked", row.checked}});
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = kStates[static_cast<int>(model_.state())];
    fields["manga"] = manga_.id;
    fields["rows"] = rows;
    if (model_.state() == CategoryPickerModel::State::Saved) {
        nlohmann::ordered_json saved = nlohmann::ordered_json::array();
        for (const core::Category& c : model_.saved()) {
            saved.push_back(c.id);
        }
        fields["saved"] = saved;
        fields["sent"] = model_.savedChange();
    }
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        app::testhooks::write("category_picker", fields);
    }
}

void CategoryPickerActivity::save() {
    if (!closing_) {
        model_.save();
    }
}

void CategoryPickerActivity::askNew() {
    if (model_.busy() || model_.state() != CategoryPickerModel::State::Ready) {
        return;
    }
    askText("categories/add_header"_i18n, "", context_.profile.categoryNameMaxChars, [this](std::string text) {
        const core::NameProblem found = model_.checkName(text);
        const int maxChars = context_.profile.categoryNameMaxChars;
        if (found == core::NameProblem::Empty) {
            brls::Application::notify("categories/problem_empty"_i18n);
        } else if (found == core::NameProblem::TooLong) {
            brls::Application::notify(brls::getStr("categories/problem_too_long", maxChars));
        } else if (found == core::NameProblem::Taken) {
            brls::Application::notify(brls::getStr("categories/problem_taken", core::trimText(text)));
        }
        if (found != core::NameProblem::None) {
            nlohmann::ordered_json fields = nlohmann::ordered_json::object();
            fields["action"] = "add";
            fields["ok"] = false;
            fields["problem"] = std::string(core::nameProblemName(found));
            app::testhooks::write("category_action", fields);
            return;
        }
        model_.addCategory(text);
    });
}

}  // namespace snx::ui
