#include "ui/setup/SetupActivity.hpp"

#include <memory>
#include <utility>

#include "api/ServerUrl.hpp"
#include "app/TestHooks.hpp"
#include "ui/components/ServerCheck.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr int kMaxUrlLength = 512;
constexpr int kMaxCredentialLength = 128;

// The selector's rows, in this order.
const core::AuthMode kAuthModes[] = {core::AuthMode::None, core::AuthMode::Basic, core::AuthMode::UiLogin};

int authIndex(core::AuthMode mode) {
    for (int i = 0; i < 3; ++i) {
        if (kAuthModes[i] == mode) {
            return i;
        }
    }
    return 0;
}

std::string warningText(SetupWarning warning) {
    switch (warning) {
        case SetupWarning::TailscaleAddress:
            return "setup/warning/tailscale"_i18n;
        case SetupWarning::PasswordOverHttp:
            return "setup/warning/password_http"_i18n;
        case SetupWarning::PasswordOnSdCard:
            return "setup/warning/password_sd"_i18n;
        case SetupWarning::UseIpAddress:
            return "setup/warning/use_ip"_i18n;
    }
    return {};
}

std::string problemText(SetupProblem problem) {
    switch (problem) {
        case SetupProblem::UrlMissing:
            return "setup/problem/url_missing"_i18n;
        case SetupProblem::UrlNotHttp:
            return "setup/problem/url_not_http"_i18n;
        case SetupProblem::UsernameMissing:
            return "setup/problem/username_missing"_i18n;
        case SetupProblem::PasswordMissing:
            return "setup/problem/password_missing"_i18n;
    }
    return {};
}

}  // namespace

SetupActivity::SetupActivity(app::AppContext& context, core::ServerSettings start, bool canGoBack,
                             std::function<void()> onSaved, std::string problem)
    : context_(context),
      model_(std::move(start)),
      canGoBack_(canGoBack),
      onSaved_(std::move(onSaved)),
      problem_(std::move(problem)) {}

SetupActivity::~SetupActivity() {
    cancel_.cancel();
}

void SetupActivity::onContentAvailable() {
    const core::ServerSettings& draft = model_.draft();
    url->setup(draft.url, false, kMaxUrlLength, "setup/url_hint"_i18n, [this](std::string text) {
        model_.setUrl(std::move(text));
        refresh();
    });
    auth->init("setup/auth"_i18n, {"setup/auth_none"_i18n, "setup/auth_basic"_i18n, "setup/auth_ui_login"_i18n},
               authIndex(draft.auth), [this](int selected) {
                   if (selected >= 0 && selected < 3) {
                       model_.setAuth(kAuthModes[selected]);
                       refresh();
                   }
               });
    username->setup(draft.username, false, kMaxCredentialLength, "", [this](std::string text) {
        model_.setUsername(std::move(text));
        refresh();
    });
    password->setup(draft.password, true, kMaxCredentialLength, "", [this](std::string text) {
        model_.setPassword(std::move(text));
        refresh();
    });
    test->registerClickAction([this](brls::View*) {
        startTest();
        return true;
    });
    next->registerClickAction([this](brls::View*) {
        if (model_.requestContinue()) {
            finish();
        } else {
            startTest();
        }
        return true;
    });
    back->setVisibility(canGoBack_ ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    back->registerClickAction([this](brls::View*) {
        cancel();
        return true;
    });
    // B: back to where the screen came from, or nothing on the first run (there is no server to go back to).
    box->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](brls::View*) {
        cancel();
        return true;
    }, !canGoBack_);

    refresh();
    showStatus(problem_);
    if (!problem_.empty()) {
        app::testhooks::error("Setup", "auth", problem_);
    }
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["firstRun"] = !canGoBack_;
    params["url"] = draft.url;
    app::testhooks::screen("Setup", params);
}

void SetupActivity::refresh() {
    username->setVisibility(model_.needsCredentials() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    password->setVisibility(model_.needsCredentials() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    std::string text;
    for (const SetupWarning warning : model_.warnings()) {
        text += (text.empty() ? "" : "\n") + warningText(warning);
    }
    warnings->setText(text);
    warnings->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    if (model_.state() == SetupModel::State::Editing) {
        showStatus("");
    }
}

void SetupActivity::showStatus(const std::string& text) {
    status->setText(text);
    status->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void SetupActivity::startTest() {
    if (const auto problem = model_.problem()) {
        showStatus(problemText(*problem));
        return;
    }
    if (!model_.testStarted()) {
        return;  // one is running
    }
    showStatus("setup/testing"_i18n);
    const core::ServerSettings settings = model_.settingsToTest();
    brls::Logger::info("setup: testing {} ({})", settings.url, std::string(core::authModeName(settings.auth)));
    context_.client.configure(settings);

    cancel_.cancel();
    cancel_ = net::CancelToken();
    const net::CancelToken token = cancel_;
    if (settings.auth != core::AuthMode::UiLogin || !settings.refreshToken.empty()) {
        checkServer(context_.client, token, [this](const api::ApiResult<core::ServerInfo>& result) { onTested(result); });
        return;
    }
    // ui_login without a saved login: log in first (a wrong password then says so plainly), then the usual check.
    context_.client.login(settings.username, settings.password, token)
        .then([this, token](const api::ApiResult<core::LoginTokens>& login) {
            if (token.cancelled()) {
                return;
            }
            if (!login.ok()) {
                onTested(api::ApiResult<core::ServerInfo>(login.error()));
                return;
            }
            checkServer(context_.client, token, [this](const api::ApiResult<core::ServerInfo>& result) { onTested(result); });
        });
}

void SetupActivity::onTested(const api::ApiResult<core::ServerInfo>& result) {
    model_.testFinished(result);
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["where"] = "setup";
    fields["ok"] = result.ok();
    if (result.ok()) {
        fields["version"] = result.value().version;
    } else {
        fields["kind"] = api::apiErrorKindName(result.error().kind);
    }
    app::testhooks::write("server_check", fields);

    if (model_.state() == SetupModel::State::Passed) {
        const core::ServerInfo& info = model_.server();
        std::string text = "setup/connected"_i18n + " " + info.name + " " + info.version + " (" + info.buildType + ")";
        if (const auto warning = model_.serverWarning()) {
            text += "\n" + *warning;
        }
        brls::Logger::info("setup: {}", text);
        showStatus(text);
        if (model_.continueAfterTest()) {
            finish();
        }
    } else if (model_.state() == SetupModel::State::Failed) {
        const std::string text = errorText(model_.error());
        brls::Logger::warning("setup: test failed: {} ({})", text, model_.error().detail);
        app::testhooks::error("Setup", api::apiErrorKindName(model_.error().kind), text);
        showStatus(text);
    } else {
        showStatus("");  // the settings changed while it ran
    }
}

void SetupActivity::finish() {
    if (closing_) {
        return;
    }
    closing_ = true;
    // The client holds the tested settings, with fresh tokens for ui_login.
    context_.config.setServer(context_.client.settings());
    brls::Logger::info("setup: saved {}", context_.client.settings().url);
    std::function<void()> onSaved = onSaved_;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [onSaved] {
        if (onSaved) {
            onSaved();
        }
    });
}

void SetupActivity::cancel() {
    if (!canGoBack_ || closing_) {
        return;
    }
    closing_ = true;
    cancel_.cancel();
    context_.client.configure(context_.config.config().server);  // back to what is saved
    brls::Application::popActivity();
}

}  // namespace snx::ui
