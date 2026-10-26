#include "ui/main/MainActivity.hpp"

#include <utility>

#include "app/TestHooks.hpp"
#include "ui/components/ServerCheck.hpp"
#include "ui/login/LoginActivity.hpp"
#include "ui/main/SettingsTab.hpp"
#include "ui/setup/SetupActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr std::size_t kLibraryTab = 0;
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

}  // namespace

MainActivity::MainActivity(app::AppContext& context)
    : context_(context), model_(context.config.serverSetUp()), covered_(!context.config.serverSetUp()) {}

MainActivity::~MainActivity() {
    cancel_.cancel();
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
    } else {
        auto* status = new TabStatus(*this);
        if (index == kLibraryTab) {
            libraryView_ = status;
        } else {
            status->showMessage(brls::getStr(kTabLabels[index]), "main/coming_soon"_i18n);
        }
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
            libraryView_->showMessage("main/not_set_up"_i18n, "");
            break;
        case MainModel::Connection::Checking:
            libraryView_->showLoading("main/connecting"_i18n);
            break;
        case MainModel::Connection::Connected: {
            const core::ServerInfo& info = model_.server();
            std::string text = "main/library_soon"_i18n;
            if (const auto warning = model_.serverWarning()) {
                text = *warning + "\n" + text;
            }
            libraryView_->showMessage("main/connected"_i18n + " " + info.name + " " + info.version, text);
            break;
        }
        case MainModel::Connection::Failed:
            libraryView_->showError("Library", model_.error(), [this] { retry(); });
            break;
    }
}

void MainActivity::retry() {
    startCheck();
}

void MainActivity::serverChanged() {
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
    openLogin({});
}

}  // namespace snx::ui
