#include "ui/browse/FiltersActivity.hpp"

#include <utility>

#include "app/ConfigStore.hpp"
#include "app/TestHooks.hpp"
#include "core/LibraryTools.hpp"
#include "core/SourceFilters.hpp"
#include "ui/components/AskText.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of 72 px rows (D16).
constexpr float kRowWidth = 1120;
constexpr float kRowHeight = 72;
constexpr GridMetrics kListMetrics{kRowWidth, kRowHeight, 4, 0};
constexpr float kIndent = 48;
constexpr float kValueWidth = 320;  // "Latest chapter, descending" fits
const NVGcolor kSecondaryText = nvgRGB(150, 150, 156);

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

std::string triText(core::TriState tri) {
    switch (tri) {
        case core::TriState::Include:
            return "browse/include"_i18n;
        case core::TriState::Exclude:
            return "browse/exclude"_i18n;
        case core::TriState::Ignore:
            break;
    }
    return "browse/any"_i18n;
}

// What a row shows after the name ("" for a header or a group heading).
std::string valueText(const core::SourceFilter& filter, const core::FilterValue* value) {
    if (value == nullptr) {
        return {};
    }
    switch (filter.kind) {
        case core::FilterKind::Text:
            return value->text.empty() ? "browse/any"_i18n : value->text;
        case core::FilterKind::Select:
            return value->select >= 0 && static_cast<std::size_t>(value->select) < filter.values.size()
                       ? filter.values[static_cast<std::size_t>(value->select)]
                       : std::string();
        case core::FilterKind::CheckBox:
            return value->check ? "browse/on"_i18n : "browse/off"_i18n;
        case core::FilterKind::TriState:
            return triText(value->tri);
        case core::FilterKind::Sort:
            if (!value->sort || value->sort->index < 0 ||
                static_cast<std::size_t>(value->sort->index) >= filter.values.size()) {
                return "browse/any"_i18n;
            }
            return filter.values[static_cast<std::size_t>(value->sort->index)] + ", " +
                   (value->sort->ascending ? "browse/ascending"_i18n : "browse/descending"_i18n);
        default:
            return {};
    }
}

// One filter: its name (indented in a group) and value.
class FilterCell : public GridCell {
  public:
    FilterCell() {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);
        setBackgroundColor(nvgRGBA(128, 128, 128, 24));
        setPaddingRight(20);
        // Fixed widths: a label that may grow and shrink got cut short ("Genres (1 …") when the value was empty.
        name_ = new brls::Label();
        name_->setFontSize(20);
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        addView(name_);
        value_ = new brls::Label();
        value_->setWidth(kValueWidth);
        value_->setFontSize(20);
        value_->setSingleLine(true);
        value_->setAutoAnimate(false);
        value_->setMarginLeft(20);
        value_->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        value_->setTextColor(kSecondaryText);
        addView(value_);
    }

    void show(const std::string& name, const std::string& value, int depth, bool header) {
        const float indent = kIndent * static_cast<float>(depth);
        setPaddingLeft(20 + indent);
        name_->setWidth(kRowWidth - 40 - indent - 20 - kValueWidth);
        name_->setText(name);
        name_->setTextColor(header ? kSecondaryText : brls::Application::getTheme()["brls/text"]);
        value_->setText(value);
    }

  private:
    brls::Label* name_;
    brls::Label* value_;
};

}  // namespace

FiltersActivity::FiltersActivity(app::AppContext& context, SourceFiltersModel& model, std::string sourceName,
                                 std::function<void()> onApply, std::function<void()> onClosed)
    : context_(context),
      model_(model),
      sourceName_(std::move(sourceName)),
      onApply_(std::move(onApply)),
      onClosed_(std::move(onClosed)) {}

FiltersActivity::~FiltersActivity() {
    model_.setListener({});
    if (onClosed_) {
        onClosed_();  // the source screen takes the model's listener back
    }
    if (list_ != nullptr) {
        list_->setSource(nullptr);
    }
}

void FiltersActivity::onContentAvailable() {
    if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
        frame->setTitle(brls::getStr("browse/filters_title", sourceName_));
    }
    status_ = new StatusView();
    status_->setGrow(1.0f);
    body->addView(status_);
    list_ = new CoverGrid(kListMetrics);
    list_->setGrow(1.0f);
    list_->setVisibility(brls::Visibility::GONE);
    body->addView(list_);
    applyButton->registerClickAction([this](brls::View*) {
        apply();
        return true;
    });
    resetButton->registerClickAction([this](brls::View*) {
        reset();
        return true;
    });
    getContentView()->registerAction("browse/apply"_i18n, brls::BUTTON_X, [this](brls::View*) {
        apply();
        return true;
    });
    getContentView()->registerAction("browse/reset"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        reset();
        return true;
    });
    ready_ = true;
    openedAtMs_ = app::appClockMs();
    model_.setListener([this] { modelChanged(); });
    list_->setSource(this);
    modelChanged();
    app::testhooks::screen("Filters", nlohmann::ordered_json{{"source", sourceName_}});
}

std::size_t FiltersActivity::count() const {
    return editing_ ? model_.rows().size() : 0;
}

GridCell* FiltersActivity::makeCell() {
    return new FilterCell();
}

void FiltersActivity::bind(GridCell& cell, std::size_t index) {
    const core::SourceFilter* filter = model_.filterAt(index);
    if (filter == nullptr) {
        return;
    }
    cell.setId("filters/row-" + std::to_string(index));  // named in "focus" test events
    std::string name = filter->name;
    if (filter->kind == core::FilterKind::Group) {
        const int set = model_.groupChangedCount(model_.rows()[index].filter);
        if (set > 0) {
            name = brls::getStr("browse/group_count", filter->name, set);
        }
    }
    static_cast<FilterCell&>(cell).show(name, valueText(*filter, model_.valueAt(index)), model_.rows()[index].depth,
                                        filter->kind == core::FilterKind::Header);
}

void FiltersActivity::unbind(GridCell&) {}

void FiltersActivity::select(std::size_t index) {
    const core::SourceFilter* filter = model_.filterAt(index);
    const core::FilterValue* value = model_.valueAt(index);
    if (filter == nullptr || closing_) {
        return;
    }
    switch (filter->kind) {
        case core::FilterKind::Group:
            model_.toggleGroup(index);
            break;
        case core::FilterKind::CheckBox:
            model_.toggleCheck(index);
            break;
        case core::FilterKind::TriState:
            model_.cycleTri(index);
            break;
        case core::FilterKind::Text: {
            const int maxChars = context_.profile.filterTextMaxChars;
            askText(filter->name, value != nullptr ? value->text : std::string(), maxChars, [this, index, maxChars](std::string text) {
                model_.setText(index, core::utf8Cut(core::trimText(text), static_cast<std::size_t>(maxChars)));
            });
            break;
        }
        case core::FilterKind::Select:
        case core::FilterKind::Sort: {
            if (filter->values.empty()) {
                break;
            }
            const bool sort = filter->kind == core::FilterKind::Sort;
            int current = 0;
            if (value != nullptr) {
                current = sort ? (value->sort ? value->sort->index : 0) : value->select;
            }
            auto* dropdown = new brls::Dropdown(filter->name, filter->values, [this, index, sort](int selected) {
                if (selected < 0) {
                    return;
                }
                sort ? model_.chooseSort(index, selected) : model_.setSelect(index, selected);
            }, current);
            brls::Application::pushActivity(new brls::Activity(dropdown));
            break;
        }
        default:
            break;  // a header: nothing
    }
}

void FiltersActivity::showList(bool visible) {
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
            brls::Application::giveFocus(status_);
        }
    }
}

void FiltersActivity::modelChanged() {
    if (!ready_ || closing_) {
        return;
    }
    switch (model_.state()) {
        case SourceFiltersModel::State::Loading:
            status_->showLoading("browse/filters_loading"_i18n);
            showList(false);
            break;
        case SourceFiltersModel::State::Failed:
            status_->showError("Filters", model_.error(), [this] { model_.retry(); });
            showList(false);
            break;
        case SourceFiltersModel::State::Ready:
            if (!editing_) {
                editing_ = true;
                model_.beginEdit();  // a fresh copy of the applied values, groups closed
            }
            if (model_.rows().empty()) {
                status_->showMessage("browse/filters"_i18n, "browse/filters_none"_i18n);
                showList(false);
                break;
            }
            if (!listShown_) {
                showList(true);
                list_->reload(0);
            } else if (model_.rows().size() != shownRows_) {
                list_->reload(list_->focusIndex());  // a group opened or closed: the focus stays on its heading
            } else if (model_.revision() != shownRevision_) {
                list_->rebind();
            }
            break;
    }
    shownRevision_ = model_.revision();
    shownRows_ = listShown_ ? model_.rows().size() : 0;
    const int changed = editing_ ? model_.editCount() : 0;
    note->setText(changed > 0 ? brls::getStr("browse/filters_count", changed) : "browse/filters"_i18n);
    report();
}

void FiltersActivity::report() {
    if (model_.state() == SourceFiltersModel::State::Loading || (model_.state() == SourceFiltersModel::State::Ready && !editing_)) {
        return;
    }
    static const char* const kStates[] = {"loading", "ready", "failed"};
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (std::size_t i = 0; editing_ && i < model_.rows().size(); ++i) {
        const core::SourceFilter* filter = model_.filterAt(i);
        if (filter == nullptr) {
            continue;
        }
        const SourceFiltersModel::Row& row = model_.rows()[i];
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["kind"] = std::string(core::filterKindName(filter->kind));
        entry["name"] = filter->name;
        entry["value"] = valueText(*filter, model_.valueAt(i));
        entry["depth"] = row.depth;
        if (filter->kind == core::FilterKind::Group) {
            entry["open"] = model_.isOpen(row.filter);
        }
        rows.push_back(entry);
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = kStates[static_cast<int>(model_.state())];
    fields["rows"] = rows;
    fields["changed"] = editing_ ? model_.editCount() : 0;
    const std::string key = fields.dump();
    if (key != reported_) {
        const bool first = reported_.empty();
        reported_ = key;
        if (first) {
            fields["ms"] = app::appClockMs() - openedAtMs_;  // opened → ready (D56: ≤ 0.5 s)
        }
        app::testhooks::write("filters", fields);
    }
}

void FiltersActivity::apply() {
    if (closing_ || !editing_ || model_.state() != SourceFiltersModel::State::Ready) {
        return;
    }
    closing_ = true;
    model_.apply();
    app::testhooks::write("filters_applied", nlohmann::ordered_json{{"changed", model_.appliedCount()},
                                                                   {"changes", model_.appliedChanges()}});
    if (onApply_) {
        onApply_();
    }
    brls::sync([] { brls::Application::popActivity(); });
}

void FiltersActivity::reset() {
    if (!closing_ && editing_) {
        model_.reset();
    }
}

}  // namespace snx::ui
