#include "ui/browse/BrowseTab.hpp"

#include <utility>

#include "app/TestHooks.hpp"
#include "core/SourceLanguages.hpp"
#include "ui/browse/SourceActivity.hpp"
#include "ui/components/CoverImage.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of 80 px rows (D16: over the 72 px touch minimum) in the 870 px wide tab.
constexpr float kRowWidth = 800;
constexpr float kRowHeight = 80;
constexpr GridMetrics kListMetrics{kRowWidth, kRowHeight, 4, 24};
constexpr float kLabelWidth = 190;
constexpr float kIconSide = 56;
constexpr std::size_t kReportedRows = 40;  // the "sources" test event lists at most this many

const NVGcolor kSecondaryText = nvgRGB(150, 150, 156);
const NVGcolor kNsfwTag = nvgRGB(190, 40, 60);

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

std::string rowLabel(const core::SourceRow& row) {
    if (row.label.empty()) {
        return {};
    }
    return row.lastUsed ? "browse/last_used"_i18n : row.label;
}

// One source: its language label (first row of a language only), icon, name and the 18+ tag.
class SourceCell : public GridCell {
  public:
    SourceCell() {
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setCornerRadius(4);
        setHighlightCornerRadius(6);
        setPaddingLeft(16);
        setPaddingRight(16);
        label_ = new brls::Label();
        label_->setWidth(kLabelWidth);
        label_->setFontSize(16);
        label_->setTextColor(kSecondaryText);
        label_->setSingleLine(true);
        label_->setAutoAnimate(false);
        addView(label_);
        icon_ = new CoverImage(kIconSide, kIconSide);
        addView(icon_);
        name_ = new brls::Label();
        name_->setWidth(kRowWidth - 32 - kLabelWidth - kIconSide - 20 - 70);
        name_->setMarginLeft(20);
        name_->setFontSize(20);
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        addView(name_);
        tagBox_ = new brls::Box();
        tagBox_->setBackgroundColor(kNsfwTag);
        tagBox_->setCornerRadius(4);
        tagBox_->setPadding(2, 8, 2, 8);
        tagBox_->setMarginLeft(12);
        auto* tag = new brls::Label();
        tag->setFontSize(14);
        tag->setTextColor(nvgRGB(255, 255, 255));
        tag->setText("browse/nsfw"_i18n);
        tag->setAutoAnimate(false);
        tagBox_->addView(tag);
        addView(tagBox_);
    }

    void show(const core::SourceRow& row, const core::Source& source, app::AppContext& context) {
        label_->setText(rowLabel(row));
        name_->setText(source.displayName.empty() ? source.name : source.displayName);
        tagBox_->setVisibility(source.contentWarning == core::ContentWarning::Nsfw ? brls::Visibility::VISIBLE
                                                                                     : brls::Visibility::INVISIBLE);
        icon_->load(source.iconUrl.empty() ? std::string() : context.client.absoluteUrl(source.iconUrl), context);
    }

    void clear() { icon_->clear(); }

  private:
    brls::Label* label_;
    CoverImage* icon_;
    brls::Label* name_;
    brls::Box* tagBox_;
};

}  // namespace

BrowseTab::BrowseTab(app::AppContext& context, std::function<void(const core::Manga&)> onLibraryChanged)
    : context_(context), onLibraryChanged_(std::move(onLibraryChanged)) {
    inflateFromXMLRes("xml/tabs/browse.xml");
    setDefaultFocusedIndex(1);  // from the tab list the focus lands on the sources, not on the bar
    status_ = new StatusView();
    status_->setGrow(1.0f);
    body->addView(status_);
    list_ = new CoverGrid(kListMetrics);
    list_->setGrow(1.0f);
    list_->setVisibility(brls::Visibility::GONE);
    body->addView(list_);
    refreshButton->registerClickAction([this](brls::View*) {
        refresh();
        return true;
    });
    registerAction("browse/refresh"_i18n, brls::BUTTON_X, [this](brls::View*) {
        refresh();
        return true;
    });
}

BrowseTab::~BrowseTab() {
    list_->setSource(nullptr);
    if (model_ != nullptr) {
        model_->setListener({});
    }
}

void BrowseTab::attach(SourcesModel* model) {
    if (model == model_) {
        return;
    }
    if (model_ != nullptr) {
        model_->setListener({});
    }
    list_->setSource(nullptr);
    model_ = model;
    reported_.clear();
    if (model_ == nullptr) {
        showList(false);
        return;
    }
    model_->setListener([this] { modelChanged(); });
    list_->setSource(this);
    model_->start();  // only the first time
    modelChanged();
}

void BrowseTab::showList(bool visible) {
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

void BrowseTab::modelChanged() {
    if (model_ == nullptr) {
        return;
    }
    note->setText(model_->state() == SourcesModel::State::OnlyLocal ? "browse/only_local"_i18n : "browse/sources"_i18n);
    switch (model_->state()) {
        case SourcesModel::State::Loading:
            status_->showLoading("browse/loading_sources"_i18n);
            showList(false);
            break;
        case SourcesModel::State::Failed:
            status_->showError("Browse", model_->error(), [this] { refresh(); });
            showList(false);
            break;
        case SourcesModel::State::Ready:
        case SourcesModel::State::OnlyLocal:
            if (model_->rows().empty()) {
                status_->showMessage("browse/no_sources"_i18n, "browse/only_local"_i18n);
                showList(false);
                break;
            }
            if (!listShown_ || model_->revision() != shownRevision_) {
                showList(true);
                list_->reload(model_->rememberedFocus());
            }
            break;
    }
    shownRevision_ = model_->revision();
    report();
}

void BrowseTab::report() {
    if (model_ == nullptr || model_->state() == SourcesModel::State::Loading) {
        reported_.clear();  // after a refresh the same list is reported again
        return;
    }
    static const char* const kStates[] = {"loading", "ready", "only_local", "failed"};
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (std::size_t i = 0; i < model_->rows().size() && i < kReportedRows; ++i) {
        const core::SourceRow& row = model_->rows()[i];
        const core::Source* source = model_->sourceAt(i);
        if (source == nullptr) {
            continue;
        }
        nlohmann::ordered_json entry = nlohmann::ordered_json::object();
        entry["id"] = source->id;
        entry["name"] = source->displayName;
        entry["lang"] = source->lang;
        entry["nsfw"] = source->contentWarning == core::ContentWarning::Nsfw;
        entry["label"] = rowLabel(row);
        entry["lastUsed"] = row.lastUsed;
        rows.push_back(entry);
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = kStates[static_cast<int>(model_->state())];
    fields["count"] = model_->rows().size();
    fields["rows"] = rows;
    fields["lastUsed"] = model_->lastSourceId();
    fields["hidden"] = model_->hiddenLanguages();
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        app::testhooks::write("sources", fields);
    }
}

std::size_t BrowseTab::count() const {
    if (model_ == nullptr) {
        return 0;
    }
    const auto state = model_->state();
    return state == SourcesModel::State::Ready || state == SourcesModel::State::OnlyLocal ? model_->rows().size() : 0;
}

GridCell* BrowseTab::makeCell() {
    return new SourceCell();
}

void BrowseTab::bind(GridCell& cell, std::size_t index) {
    cell.setId("browse/source-" + std::to_string(index));  // named in "focus" test events
    const core::Source* source = model_ != nullptr ? model_->sourceAt(index) : nullptr;
    if (source != nullptr) {
        static_cast<SourceCell&>(cell).show(model_->rows()[index], *source, context_);
    }
}

void BrowseTab::unbind(GridCell& cell) {
    static_cast<SourceCell&>(cell).clear();
}

void BrowseTab::focused(std::size_t index) {
    if (model_ != nullptr) {
        model_->rememberFocus(index);
    }
}

void BrowseTab::select(std::size_t index) {
    if (model_ == nullptr) {
        return;
    }
    const core::Source* found = model_->sourceAt(index);
    if (found == nullptr) {
        return;
    }
    const core::Source source = *found;
    model_->rememberFocus(index);
    model_->opened(source.id);  // Last used (the list gets its row; the focus stays on this source)
    core::BrowseSettings browse = context_.config.config().browse;
    browse.lastSourceId = source.id;
    context_.config.setBrowse(browse);
    brls::Application::pushActivity(new SourceActivity(context_, source, onLibraryChanged_));
}

void BrowseTab::refresh() {
    if (model_ != nullptr) {
        model_->refresh();
    }
}

}  // namespace snx::ui
