#include "ui/browse/LanguagesActivity.hpp"

#include <utility>

#include "app/TestHooks.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// One column of rows, 80 px high (D16: over the 72 px touch minimum), as in the category picker.
constexpr float kRowWidth = 1120;
constexpr float kRowHeight = 80;
constexpr GridMetrics kListMetrics{kRowWidth, kRowHeight, 4, 0};
constexpr float kBoxSide = 32;

NVGcolor themeColor(const char* name) {
    return brls::Application::getTheme()[name];
}

// One language with its tick box (drawn as boxes: no font needed), like the category picker's rows.
class LanguageCell : public GridCell {
  public:
    LanguageCell() {
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

    void show(const LanguagesModel::Row& row) {
        name_->setText(row.name);
        tick_->setBackgroundColor(row.shown ? themeColor("brls/accent") : nvgRGBA(0, 0, 0, 0));
    }

  private:
    brls::Box* box_;
    brls::Box* tick_;
    brls::Label* name_;
};

}  // namespace

LanguagesActivity::LanguagesActivity(app::AppContext& context, const std::vector<std::string>& languages,
                                     std::vector<std::string> hidden,
                                     std::function<void(std::vector<std::string>)> onDone)
    : model_(languages, std::move(hidden)), onDone_(std::move(onDone)) {
    (void)context;
}

LanguagesActivity::~LanguagesActivity() {
    if (list_ != nullptr) {
        list_->setSource(nullptr);
    }
}

void LanguagesActivity::onContentAvailable() {
    if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
        frame->setTitle("browse/languages_title"_i18n);
    }
    list_ = new CoverGrid(kListMetrics);
    list_->setGrow(1.0f);
    body->addView(list_);
    doneButton->registerClickAction([this](brls::View*) {
        done();
        return true;
    });
    getContentView()->registerAction("browse/done"_i18n, brls::BUTTON_X, [this](brls::View*) {
        done();
        return true;
    });
    list_->setSource(this);
    app::testhooks::screen("Languages");
    report("open");
}

std::size_t LanguagesActivity::count() const {
    return model_.rows().size();
}

GridCell* LanguagesActivity::makeCell() {
    return new LanguageCell();
}

void LanguagesActivity::bind(GridCell& cell, std::size_t index) {
    cell.setId("languages/row-" + std::to_string(index));  // named in "focus" test events
    if (index < model_.rows().size()) {
        static_cast<LanguageCell&>(cell).show(model_.rows()[index]);
    }
}

void LanguagesActivity::unbind(GridCell&) {}

void LanguagesActivity::select(std::size_t index) {
    if (closing_) {
        return;
    }
    if (!model_.toggle(index)) {
        if (index < model_.rows().size()) {
            brls::Application::notify("browse/languages_one"_i18n);  // the last one shown stays
            report("refused");
        }
        return;
    }
    list_->rebind();
    report("open");
}

void LanguagesActivity::done() {
    if (closing_) {
        return;
    }
    closing_ = true;
    report("done");
    if (model_.changed() && onDone_) {
        onDone_(model_.hidden());
    }
    brls::sync([] { brls::Application::popActivity(); });
}

void LanguagesActivity::report(const char* state) {
    nlohmann::ordered_json rows = nlohmann::ordered_json::array();
    for (const LanguagesModel::Row& row : model_.rows()) {
        rows.push_back({{"code", row.code}, {"name", row.name}, {"shown", row.shown}});
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["state"] = state;
    fields["rows"] = rows;
    fields["hidden"] = model_.hidden();
    fields["changed"] = model_.changed();
    app::testhooks::write("languages", fields);
}

}  // namespace snx::ui
