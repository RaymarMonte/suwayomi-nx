#include "ui/main/MainActivity.hpp"

#include <chrono>
#include <utility>

#include "app/TestHooks.hpp"
#include "ui/components/ServerCheck.hpp"
#include "ui/library/LibraryTab.hpp"
#include "ui/login/LoginActivity.hpp"
#include "ui/main/SettingsTab.hpp"
#include "ui/setup/SetupActivity.hpp"
#include "ui/updates/ChapterRowsTab.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr std::size_t kLibraryTab = 0;
constexpr std::size_t kUpdatesTab = 1;
constexpr std::size_t kHistoryTab = 2;
constexpr std::size_t kSettingsTab = 4;

// The i18n key of each tab's label, in MainModel::tabs() order.
const char* const kTabLabels[] = {"main/library", "main/updates", "main/history", "main/browse", "main/settings"};

// A tab's StatusView that tells the main screen when the frame deletes it.
class TabStatus : public StatusView {
  public:
    explicit TabStatus(MainActivity& main) : main_(main) {}
    ~TabStatus() override { main_.forgetTab(this); }

  private:
    MainActivity& main_;
};

// The Library tab, telling the main screen the same.
class LibraryTabView : public LibraryTab {
  public:
    explicit LibraryTabView(MainActivity& main) : LibraryTab(main.context()), main_(main) {}
    ~LibraryTabView() override { main_.forgetTab(this); }

  private:
    MainActivity& main_;
};

}  // namespace

MainActivity::MainActivity(app::AppContext& context)
    : context_(context), model_(context.config.serverSetUp()), covered_(!context.config.serverSetUp()) {}

MainActivity::~MainActivity() {
    cancel_.cancel();
    if (updater_) {
        updater_->removeListener(updaterListener_);
    }
    // The tab's view outlives this destructor (borealis deletes the content afterwards): let go of the model first.
    if (libraryView_ != nullptr) {
        libraryView_->showStatusOnly();
    }
}

void MainActivity::onContentAvailable() {
    for (std::size_t i = 0; i < MainModel::tabs().size(); ++i) {
        tabs->addTab(brls::getStr(kTabLabels[i]), [this, i] { return makeTab(i); });
    }
    if (context_.config.serverSetUp()) {
        startCheck();
    } else {
        // The first run: the setup screen goes on top once this one is on screen.
        brls::sync([this] { openSetup(true, {}); });
    }
}

brls::View* MainActivity::makeTab(std::size_t index) {
    currentTab_ = index;
    brls::View* view = nullptr;
    if (index == kSettingsTab) {
        settingsView_ = new SettingsTab(*this);
        view = settingsView_;
    } else if (index == kLibraryTab) {
        libraryView_ = new LibraryTabView(*this);
        view = libraryView_;
    } else if ((index == kUpdatesTab || index == kHistoryTab) &&
               model_.connection() == MainModel::Connection::Connected) {
        view = makeRowsTab(index == kUpdatesTab);
    } else if (index == kUpdatesTab || index == kHistoryTab) {
        auto* status = new TabStatus(*this);
        status->showMessage(brls::getStr(kTabLabels[index]), "main/connect_first"_i18n);
        view = status;
    } else {
        auto* status = new TabStatus(*this);
        status->showMessage(brls::getStr(kTabLabels[index]), "main/coming_soon"_i18n);
        view = status;
    }
    if (index == kLibraryTab) {
        showConnection();
    }
    reportScreen();
    return view;
}

void MainActivity::forgetTab(const brls::View* tab) {
    if (tab == libraryView_) {
        libraryView_ = nullptr;
    }
    if (tab == settingsView_) {
        settingsView_ = nullptr;
    }
}

void MainActivity::reportScreen() {
    if (covered_) {
        return;
    }
    const std::string_view name = MainModel::tabs()[currentTab_];
    if (currentTab_ == kSettingsTab) {
        app::testhooks::screen(name, SettingsTab::aboutParams(context_));
    } else {
        app::testhooks::screen(name);
    }
}

void MainActivity::onResume() {
    covered_ = false;
    reportScreen();
}

void MainActivity::startCheck() {
    cancel_.cancel();
    cancel_ = net::CancelToken();
    if (!model_.checkStarted()) {
        return;
    }
    showConnection();
    const net::CancelToken token = cancel_;
    brls::Logger::info("main: checking {}", context_.client.settings().url);
    checkServer(context_.client, token, [this](const api::ApiResult<core::ServerInfo>& result) { onCheck(result); });
}

void MainActivity::onCheck(const api::ApiResult<core::ServerInfo>& result) {
    const core::AuthMode auth = context_.client.settings().auth;
    const MainModel::Next next = model_.checkFinished(result, auth);
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["where"] = "main";
    fields["ok"] = result.ok();
    if (result.ok()) {
        fields["version"] = result.value().version;
        brls::Logger::info("main: connected to {} {}", result.value().name, result.value().version);
        if (const auto warning = model_.serverWarning()) {
            brls::Application::notify(*warning);  // D9: too old a server; the library still tries
        }
    } else {
        fields["kind"] = api::apiErrorKindName(result.error().kind);
    }
    app::testhooks::write("server_check", fields);
    showConnection();
    if (settingsView_ != nullptr) {
        settingsView_->refresh();
    }
    if (next == MainModel::Next::ShowLogin) {
        openLogin(errorText(model_.error()));
    } else if (next == MainModel::Next::ShowSetup) {
        openSetup(false, errorText(model_.error()));
    }
}

void MainActivity::showConnection() {
    if (libraryView_ == nullptr) {
        return;
    }
    switch (model_.connection()) {
        case MainModel::Connection::NotSetUp:
            libraryView_->status().showMessage("main/not_set_up"_i18n, "");
            libraryView_->showStatusOnly();
            break;
        case MainModel::Connection::Checking:
            libraryView_->status().showLoading("main/connecting"_i18n);
            libraryView_->showStatusOnly();
            break;
        case MainModel::Connection::Connected:
            if (!library_) {
                library_ = std::make_unique<LibraryModel>(context_.client, context_.config.config().library,
                                                          context_.profile.libraryPageSize);
            }
            libraryView_->attach(library_.get());
            library_->start();  // only the first time
            break;
        case MainModel::Connection::Failed:
            libraryView_->status().showError("Library", model_.error(), [this] { retry(); });
            libraryView_->showStatusOnly();
            break;
    }
}

void MainActivity::dropLibrary() {
    if (libraryView_ != nullptr) {
        libraryView_->showStatusOnly();
    }
    library_.reset();
    // Another server or login: other updates and history. A tab view still showing keeps its own share until it goes.
    if (updater_) {
        updater_->removeListener(updaterListener_);
    }
    updater_.reset();
    updates_.reset();
    history_.reset();
    updaterFinished_ = 0;
    updaterReported_.clear();
}

void MainActivity::ensureLists() {
    const core::RuntimeProfile& p = context_.profile;
    const std::size_t screenRows = 6;  // rows of 96 px under the Updates header on the 720 px screen
    if (!updates_) {
        updates_ = std::make_shared<ChapterRowsModel>(
            context_.client, ChapterRowsModel::Kind::Updates,
            ChapterRowsModel::Limits{p.updatesFillPages, static_cast<std::size_t>(p.maxUpdateRows), screenRows});
    }
    if (!history_) {
        history_ = std::make_shared<ChapterRowsModel>(
            context_.client, ChapterRowsModel::Kind::History,
            ChapterRowsModel::Limits{p.updatesFillPages, static_cast<std::size_t>(p.maxHistoryRows), screenRows});
    }
    if (!updater_) {
        LibraryUpdater::Limits limits;
        limits.pollMs = p.libraryUpdatePollMs;
        limits.startWaitMs = p.libraryUpdateStartWaitMs;
        limits.retryMs = p.progressSyncMs;
        limits.retryMaxMs = p.progressRetryMaxMs;
        updater_ = std::make_shared<LibraryUpdater>(
            context_.client, limits,
            [] {
                return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                     std::chrono::steady_clock::now().time_since_epoch())
                                                     .count());
            },
            [](int ms, std::function<void()> fn) { brls::delay(ms, fn); });
        updaterListener_ = updater_->addListener([this] { updaterChanged(); });
    }
}

brls::View* MainActivity::makeRowsTab(bool updates) {
    ensureLists();
    return new ChapterRowsTab(
        context_, updates ? updates_ : history_, updates ? updater_ : nullptr, [this] { listsRead(); },
        [this](const core::Manga& manga) { mangaChanged(manga); });
}

void MainActivity::listsRead() {
    // The reader opened from a row closed and its progress was saved: both lists show the new state.
    if (updates_) {
        updates_->reload();
    }
    if (history_) {
        history_->reload();
    }
}

void MainActivity::mangaChanged(const core::Manga& manga) {
    if (library_) {
        library_->updateManga(manga);  // the unread badge follows (D43)
    }
}

void MainActivity::updaterChanged() {
    const LibraryUpdater& u = *updater_;
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    const auto& status = u.lastStatus();
    if (u.pollFailed() && u.active()) {
        fields["phase"] = "poll_failed";
    } else if (u.phase() == LibraryUpdater::Phase::Starting) {
        fields["phase"] = "started";
    } else if (u.phase() == LibraryUpdater::Phase::Running) {
        fields["phase"] = "progress";
    } else if (u.phase() == LibraryUpdater::Phase::Finished && u.summary()) {
        fields["phase"] = u.summary()->stopped ? "stopped" : u.summary()->nothingToUpdate ? "nothing" : "done";
    } else {
        return;  // idle: nothing to report
    }
    if (status) {
        fields["total"] = status->total;
        fields["finished"] = status->finished;
        fields["skipped"] = status->skippedManga;
    }
    if (u.phase() == LibraryUpdater::Phase::Finished && u.summary()) {
        const LibraryUpdater::Summary& s = *u.summary();
        fields["failed"] = s.failed.size();
        fields["newChapters"] = s.newChapters;
        fields["ms"] = s.ms;
    }
    const std::string key = fields.dump();
    if (key != updaterReported_) {
        updaterReported_ = key;
        app::testhooks::write("library_update", fields);
    }
    if (u.finishedCount() != updaterFinished_ && u.summary()) {
        updaterFinished_ = u.finishedCount();
        const LibraryUpdater::Summary& s = *u.summary();
        brls::Logger::info("updates: library update done in {} ms, {} new, {} skipped, {} failed", s.ms, s.newChapters,
                           s.skippedManga, s.failed.size());
        for (const std::string& title : s.failed) {
            brls::Logger::warning("updates: failed to update \"{}\"", title);
        }
        if (updates_) {
            updates_->reload();
        }
        if (s.newChapters > 0 && library_) {
            library_->refresh();  // new unread badges (keeps the category)
        }
    }
}

void MainActivity::retry() {
    startCheck();
}

void MainActivity::serverChanged() {
    dropLibrary();  // another server or login: another library
    model_.serverChanged();
    startCheck();
    if (settingsView_ != nullptr) {
        settingsView_->refresh();
    }
}

void MainActivity::openSetup(bool firstRun, std::string problem) {
    covered_ = true;
    // The saved settings, or on the first run the test config's (test builds: "showSetup" fills in the fields).
    core::ServerSettings start = context_.config.config().server;
    if (firstRun) {
        if (const auto server = app::testhooks::testServer()) {
            start = *server;
        }
    }
    brls::Application::pushActivity(
        new SetupActivity(context_, start, !firstRun, [this] { serverChanged(); }, std::move(problem)));
}

void MainActivity::openLogin(std::string message) {
    covered_ = true;
    // "Change server" closes the login screen first, so the setup screen's result lands on this one.
    brls::Application::pushActivity(new LoginActivity(
        context_, [this] { serverChanged(); },
        [this] { brls::Application::popActivity(brls::TransitionAnimation::FADE, [this] { openSetup(false, {}); }); },
        std::move(message)));
}

void MainActivity::logout() {
    brls::Logger::info("main: logging out");
    context_.client.logout();  // drops the tokens and the password; onSettingsChanged saves that
    cancel_.cancel();
    dropLibrary();
    openLogin({});
}

}  // namespace snx::ui
