#include "ui/categories/CategoriesActivity.hpp"

#include <utility>
#include <vector>

#include "app/TestHooks.hpp"
#include "ui/components/AskText.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of rows, 80 px high (D16: over the 72 px touch minimum).
constexpr float kRowWidth = 1120;
constexpr float kRowHeight = 80;
constexpr GridMetrics kListMetrics{kRowWidth, kRowHeight, 4, 0};
constexpr float kCountWidth = 200;
const NVGcolor kSecondaryText = nvgRGB(150, 150, 150);

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

std::string countText(int count) {
    return count == 1 ? "categories/count_one"_i18n : brls::getStr("categories/count", count < 0 ? 0 : count);
}

const char* actionName(CategoriesModel::Action action) {
    switch (action) {
        case CategoriesModel::Action::Add:
            return "add";
        case CategoriesModel::Action::Rename:
            return "rename";
        case CategoriesModel::Action::Move:
            return "move";
        case CategoriesModel::Action::Delete:
            return "delete";
    }
    return "add";
}

// One category: its name and how many manga it holds.
class CategoryCell : public GridCell {
  public:
    CategoryCell() {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));
        setPaddingLeft(20);
        setPaddingRight(20);
        name_ = new brls::Label();
        name_->setWidth(kRowWidth - 40 - kCountWidth);
        name_->setFontSize(20);
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        addView(name_);
        count_ = new brls::Label();
        count_->setWidth(kCountWidth);
        count_->setFontSize(16);
        count_->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        count_->setAutoAnimate(false);
        count_->setTextColor(kSecondaryText);
        addView(count_);
    }

    void show(const core::Category& category) {
        name_->setText(category.name);
        count_->setText(countText(category.mangaCount));
    }

  private:
    brls::Label* name_;
    brls::Label* count_;
};

}  // namespace

CategoriesActivity::CategoriesActivity(app::AppContext& context, std::function<void(bool)> onClosed)
    : context_(context),
      model_(context.client, static_cast<std::size_t>(context.profile.categoryNameMaxChars)),
      onClosed_(std::move(onClosed)) {}

CategoriesActivity::~CategoriesActivity() {
    model_.setListener({});
    if (list_ != nullptr) {
        list_->setSource(nullptr);  // unbinds the rows while this screen still exists
    }
    if (onClosed_) {
        onClosed_(model_.changedAnything());
    }
}

void CategoriesActivity::onContentAvailable() {
    if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
        frame->setTitle("categories/title"_i18n);
    }
    status_ = new StatusView();
    status_->setGrow(1.0f);
    body->addView(status_);
    list_ = new CoverGrid(kListMetrics);
    list_->setGrow(1.0f);
    list_->setVisibility(brls::Visibility::GONE);
    body->addView(list_);

    addButton->registerClickAction([this](brls::View*) {
        askAdd();
        return true;
    });
    getContentView()->registerAction("categories/add"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        askAdd();
        return true;
    });
    list_->registerAction("categories/move_up"_i18n, brls::BUTTON_LB, [this](brls::View*) {
        move(-1);
        return true;
    });
    list_->registerAction("categories/move_down"_i18n, brls::BUTTON_RB, [this](brls::View*) {
        move(1);
        return true;
    });

    ready_ = true;
    model_.setListener([this] { modelChanged(); });
    list_->setSource(this);
    model_.start();
    modelChanged();
    app::testhooks::screen("Categories");
}

std::size_t CategoriesActivity::count() const {
    return model_.items().size();
}

GridCell* CategoriesActivity::makeCell() {
    return new CategoryCell();
}

void CategoriesActivity::bind(GridCell& cell, std::size_t index) {
    if (index < model_.items().size()) {
        static_cast<CategoryCell&>(cell).show(model_.items()[index]);
    }
}

void CategoriesActivity::unbind(GridCell&) {}

void CategoriesActivity::select(std::size_t index) {
    openRowMenu(index);
}

void CategoriesActivity::focused(std::size_t index) {
    model_.rememberFocus(index);
}

void CategoriesActivity::showList(bool visible) {
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
        // First switch the visibility, then move the focus: a hidden view refuses it (D42).
        if (hadFocus) {
            brls::Application::giveFocus(status_);
        }
    }
}

void CategoriesActivity::modelChanged() {
    if (!ready_) {
        return;
    }
    switch (model_.state()) {
        case CategoriesModel::State::Loading:
            status_->showLoading("categories/loading"_i18n);
            showList(false);
            break;
        case CategoriesModel::State::Failed:
            status_->showError("Categories", model_.error(), [this] { model_.retry(); });
            showList(false);
            break;
        case CategoriesModel::State::Empty:
            status_->showMessage("categories/none_title"_i18n, "categories/none_text"_i18n);
            showList(false);
            break;
        case CategoriesModel::State::Ready: {
            const bool wasShown = listShown_;
            showList(true);
            if (!wasShown || model_.revision() != shownRevision_) {
                list_->reload(model_.rememberedFocus());
            }
            break;
        }
    }
    shownRevision_ = model_.revision();

    if (const std::optional<api::ApiError> failed = model_.takeActionError()) {
        brls::Application::notify(brls::getStr("categories/failed", errorText(*failed)));
    }
    if (const std::optional<CategoriesModel::Done> done = model_.takeDone()) {
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        fields["action"] = actionName(done->action);
        fields["ok"] = done->ok;
        fields["id"] = done->categoryId;
        app::testhooks::write("category_action", fields);
    }
    report();
}

void CategoriesActivity::report() {
    if (model_.state() == CategoriesModel::State::Loading || model_.busy()) {
        return;
    }
    static const char* const kStates[] = {"loading", "ready", "empty", "failed"};
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (const core::Category& c : model_.items()) {
        rows.push_back({{"id", c.id}, {"name", c.name}, {"count", c.mangaCount}});
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = kStates[static_cast<int>(model_.state())];
    fields["rows"] = rows;
    fields["changed"] = model_.changedAnything();
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        app::testhooks::write("categories", fields);
    }
}

void CategoriesActivity::problem(CategoriesModel::Action action, core::NameProblem problem, const std::string& name) {
    const int maxChars = context_.profile.categoryNameMaxChars;
    switch (problem) {
        case core::NameProblem::None:
            return;
        case core::NameProblem::Empty:
            brls::Application::notify("categories/problem_empty"_i18n);
            break;
        case core::NameProblem::TooLong:
            brls::Application::notify(brls::getStr("categories/problem_too_long", maxChars));
            break;
        case core::NameProblem::Taken:
            brls::Application::notify(brls::getStr("categories/problem_taken", name));
            break;
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["action"] = actionName(action);
    fields["ok"] = false;
    fields["problem"] = std::string(core::nameProblemName(problem));
    app::testhooks::write("category_action", fields);
}

void CategoriesActivity::askAdd() {
    if (model_.busy() || model_.state() == CategoriesModel::State::Loading ||
        model_.state() == CategoriesModel::State::Failed) {
        return;
    }
    askText("categories/add_header"_i18n, "", context_.profile.categoryNameMaxChars, [this](std::string text) {
        const core::NameProblem found = model_.checkName(text);
        if (found != core::NameProblem::None) {
            problem(CategoriesModel::Action::Add, found, core::trimText(text));
            return;
        }
        model_.add(text);
    });
}

void CategoriesActivity::askRename(std::size_t index) {
    if (model_.busy() || index >= model_.items().size()) {
        return;
    }
    const core::Category category = model_.items()[index];
    askText(brls::getStr("categories/rename_header", category.name), category.name,
            context_.profile.categoryNameMaxChars, [this, id = category.id](std::string text) {
                // Find the row again: the list may have changed while the keyboard was open.
                for (std::size_t i = 0; i < model_.items().size(); ++i) {
                    if (model_.items()[i].id != id) {
                        continue;
                    }
                    const core::NameProblem found = model_.checkName(text, static_cast<long>(i));
                    if (found != core::NameProblem::None) {
                        problem(CategoriesModel::Action::Rename, found, core::trimText(text));
                        return;
                    }
                    model_.renameAt(i, text);
                    return;
                }
            });
}

void CategoriesActivity::askDelete(std::size_t index) {
    if (model_.busy() || index >= model_.items().size()) {
        return;
    }
    const core::Category category = model_.items()[index];
    const std::string text = category.mangaCount > 0
                                 ? brls::getStr("categories/delete_ask", category.name, category.mangaCount)
                                 : brls::getStr("categories/delete_ask_empty", category.name);
    auto* dialog = new brls::Dialog(text);
    dialog->addButton("categories/cancel"_i18n, [] {});
    dialog->addButton("categories/delete_yes"_i18n, [this, id = category.id] {
        for (std::size_t i = 0; i < model_.items().size(); ++i) {
            if (model_.items()[i].id == id) {
                model_.deleteAt(i);
                return;
            }
        }
    });
    dialog->open();
    app::testhooks::write("dialog", nlohmann::ordered_json{{"text", text}});
}

void CategoriesActivity::move(int step) {
    const std::size_t index = list_->focusIndex();
    if (listShown_ && index < model_.items().size()) {
        model_.move(index, step);
    }
}

void CategoriesActivity::openRowMenu(std::size_t index) {
    if (model_.busy() || index >= model_.items().size()) {
        return;
    }
    model_.rememberFocus(index);
    const std::vector<std::string> entries = {"categories/rename"_i18n, "categories/move_up"_i18n,
                                              "categories/move_down"_i18n, "categories/delete"_i18n};
    const int id = model_.items()[index].id;
    auto* dropdown = new brls::Dropdown(model_.items()[index].name, entries, [this, id](int selected) {
        // The menu closes first; what comes next opens on the following frame.
        brls::sync([this, id, selected] {
            for (std::size_t i = 0; i < model_.items().size(); ++i) {
                if (model_.items()[i].id != id) {
                    continue;
                }
                if (selected == 0) {
                    askRename(i);
                } else if (selected == 1) {
                    model_.move(i, -1);
                } else if (selected == 2) {
                    model_.move(i, 1);
                } else if (selected == 3) {
                    askDelete(i);
                }
                return;
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace snx::ui
