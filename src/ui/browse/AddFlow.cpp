#include "ui/browse/AddFlow.hpp"

#include <utility>

#include "app/ConfigStore.hpp"
#include "app/TestHooks.hpp"
#include "ui/categories/CategoryPickerActivity.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/manga/MangaActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

std::string sourceName(const core::Manga& manga) {
    if (!manga.source) {
        return {};
    }
    return manga.source->displayName.empty() ? manga.source->name : manga.source->displayName;
}

void dialogEvent(const std::string& text) {
    app::testhooks::write("dialog", nlohmann::ordered_json{{"text", text}});
}

}  // namespace

std::shared_ptr<AddFlow> AddFlow::make(app::AppContext& context, Done done) {
    return std::shared_ptr<AddFlow>(new AddFlow(context, std::move(done)));
}

AddFlow::AddFlow(app::AppContext& context, Done done)
    : context_(context), adder_(context.client), done_(std::move(done)) {
    adder_.setListener([this] { changed(); });
}

AddFlow::~AddFlow() {
    adder_.setListener({});
}

void AddFlow::add(const core::Manga& manga) {
    if (adder_.busy()) {
        brls::Application::notify("browse/busy"_i18n);
        return;
    }
    shown_ = LibraryAdder::Step::Idle;
    sentAtMs_ = app::appClockMs();
    adder_.add(manga, context_.config.config().browse.askCategories);
}

void AddFlow::removeFromLibrary(const core::Manga& manga) {
    if (adder_.busy()) {
        brls::Application::notify("browse/busy"_i18n);
        return;
    }
    const std::string text = brls::getStr("browse/remove_ask", manga.title);
    auto* dialog = new brls::Dialog(text);
    const std::weak_ptr<AddFlow> self = weak_from_this();
    dialog->addButton("browse/cancel"_i18n, [] {});
    dialog->addButton("browse/remove_yes"_i18n, [self, manga] {
        if (const auto flow = self.lock()) {
            flow->shown_ = LibraryAdder::Step::Idle;
            flow->sentAtMs_ = app::appClockMs();
            flow->adder_.removeFromLibrary(manga);
        }
    });
    dialog->open();
    dialogEvent(text);
}

void AddFlow::changed() {
    const LibraryAdder::Step step = adder_.step();
    if (step == shown_) {
        return;
    }
    shown_ = step;
    switch (step) {
        case LibraryAdder::Step::AskDuplicate:
            askDuplicate();
            break;
        case LibraryAdder::Step::AskCategories:
            askCategories();
            break;
        case LibraryAdder::Step::Done:
            finished();
            break;
        case LibraryAdder::Step::Failed:
            failed();
            break;
        default:
            break;  // Checking, Adding, Removing, Idle: nothing to show
    }
}

void AddFlow::askDuplicate() {
    const core::Manga* duplicate = adder_.duplicate();
    if (duplicate == nullptr) {
        return;
    }
    const std::string source = sourceName(*duplicate);
    const std::string text = source.empty() ? brls::getStr("browse/duplicate_no_source", duplicate->title)
                                            : brls::getStr("browse/duplicate", duplicate->title, source);
    auto* dialog = new brls::Dialog(text);
    dialog->setCancelable(false);  // one of the three answers: the flow waits for it
    const std::weak_ptr<AddFlow> self = weak_from_this();
    const core::Manga entry = *duplicate;
    dialog->addButton("browse/show_entry"_i18n, [self, entry] {
        const auto flow = self.lock();
        if (!flow) {
            return;
        }
        flow->adder_.cancel();
        app::AppContext& context = flow->context_;
        const Done done = flow->done_;
        brls::sync([&context, entry, done] {
            brls::Application::pushActivity(new MangaActivity(context, entry, {}, {}, done));
        });
    });
    dialog->addButton("browse/add_anyway"_i18n, [self] {
        if (const auto flow = self.lock()) {
            flow->adder_.addAnyway();
        }
    });
    dialog->addButton("browse/cancel"_i18n, [self] {
        if (const auto flow = self.lock()) {
            flow->adder_.cancel();
        }
    });
    dialog->open();
    dialogEvent(text);
}

void AddFlow::askCategories() {
    const std::weak_ptr<AddFlow> self = weak_from_this();
    PickerChoice choice;
    choice.preticked = adder_.preticked();
    choice.onAdd = [self](std::set<int> chosen, bool dontAskAgain) {
        const auto flow = self.lock();
        if (!flow) {
            return;
        }
        if (dontAskAgain) {
            core::BrowseSettings browse = flow->context_.config.config().browse;
            browse.askCategories = false;
            flow->context_.config.setBrowse(browse);
        }
        flow->sentAtMs_ = app::appClockMs();
        flow->adder_.chooseCategories(std::move(chosen));
    };
    choice.onCancel = [self] {
        if (const auto flow = self.lock()) {
            flow->adder_.cancel();
        }
    };
    brls::Application::pushActivity(new CategoryPickerActivity(context_, adder_.manga(), std::move(choice)));
}

void AddFlow::finished() {
    const core::Manga& now = adder_.result();
    const std::int64_t ms = app::appClockMs() - sentAtMs_;
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["manga"] = now.id;
    fields["ok"] = true;
    if (adder_.removing()) {
        fields["ms"] = ms;
        app::testhooks::write("library_remove", fields);
        brls::Application::notify(brls::getStr("browse/removed", now.title));
    } else {
        nlohmann::ordered_json categories = nlohmann::ordered_json::array();
        std::string names;
        for (const core::Category& c : now.categories) {
            if (c.id != core::kDefaultCategoryId) {
                categories.push_back(c.id);
                names += (names.empty() ? "" : ", ") + c.name;
            }
        }
        fields["categories"] = categories;
        fields["duplicate"] = adder_.sawDuplicate();
        fields["asked"] = adder_.asked();
        fields["ms"] = ms;
        app::testhooks::write("library_add", fields);
        brls::Application::notify(brls::getStr("browse/added", now.title,
                                               names.empty() ? "categories/default_name"_i18n : names));
    }
    brls::Logger::info("browse: manga {} {} the library in {} ms", now.id, adder_.removing() ? "left" : "joined", ms);
    if (done_) {
        done_(now);
    }
}

void AddFlow::failed() {
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["manga"] = adder_.manga().id;
    fields["ok"] = false;
    fields["kind"] = api::apiErrorKindName(adder_.error().kind);
    app::testhooks::write(adder_.removing() ? "library_remove" : "library_add", fields);
    brls::Application::notify(brls::getStr("browse/failed", errorText(adder_.error())));
}

}  // namespace snx::ui
