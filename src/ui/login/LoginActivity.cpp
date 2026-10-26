#include "ui/login/LoginActivity.hpp"

#include <utility>

#include "app/TestHooks.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {
constexpr int kMaxCredentialLength = 128;

// Test builds: the test config's password fills the field (the Switch can't type in a test). Release builds: nothing.
std::string testPassword() {
    const auto server = app::testhooks::testServer();
    return server ? server->password : std::string();
}
}  // namespace

LoginActivity::LoginActivity(app::AppContext& context, std::function<void()> onDone,
                             std::function<void()> onChangeServer, std::string message)
    : context_(context),
      model_(context.client.settings().username, testPassword()),
      onDone_(std::move(onDone)),
      onChangeServer_(std::move(onChangeServer)),
      message_(std::move(message)) {}

LoginActivity::~LoginActivity() {
    cancel_.cancel();
}

void LoginActivity::onContentAvailable() {
    username->setup(model_.username(), false, kMaxCredentialLength, "", [this](std::string text) {
        model_.setUsername(std::move(text));
        showStatus("");
    });
    password->setup(model_.password(), true, kMaxCredentialLength, "", [this](std::string text) {
        model_.setPassword(std::move(text));
        showStatus("");
    });
    login->registerClickAction([this](brls::View*) {
        startLogin();
        return true;
    });
    server->registerClickAction([this](brls::View*) {
        if (onChangeServer_) {
            onChangeServer_();
        }
        return true;
    });
    box->registerAction("hints/back"_i18n, brls::BUTTON_B, [](brls::View*) { return true; }, true);
    showStatus(message_);
    if (!message_.empty()) {
        app::testhooks::error("Login", "auth", message_);
    }
    app::testhooks::screen("Login");
}

void LoginActivity::showStatus(const std::string& text) {
    status->setText(text);
    status->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void LoginActivity::startLogin() {
    if (const auto problem = model_.problem()) {
        showStatus(*problem == LoginModel::Problem::UsernameMissing ? "login/username_missing"_i18n
                                                                    : "login/password_missing"_i18n);
        return;
    }
    if (!model_.loginStarted()) {
        return;
    }
    showStatus("login/logging_in"_i18n);
    cancel_.cancel();
    cancel_ = net::CancelToken();
    const net::CancelToken token = cancel_;
    brls::Logger::info("login: logging in as {}", model_.username());
    context_.client.login(model_.username(), model_.password(), token)
        .then([this, token](const api::ApiResult<core::LoginTokens>& result) {
            if (token.cancelled()) {
                return;
            }
            model_.loginFinished(result.ok() ? api::ApiResult<bool>(true) : api::ApiResult<bool>(result.error()));
            nlohmann::ordered_json fields = nlohmann::ordered_json::object();
            fields["ok"] = result.ok();
            if (!result.ok()) {
                fields["kind"] = api::apiErrorKindName(result.error().kind);
            }
            app::testhooks::write("login_screen", fields);
            if (model_.state() == LoginModel::State::Failed) {
                const std::string text = errorText(model_.error());
                brls::Logger::warning("login: failed: {} ({})", text, model_.error().detail);
                app::testhooks::error("Login", api::apiErrorKindName(model_.error().kind), text);
                showStatus(text);
                return;
            }
            if (model_.state() != LoginModel::State::Done || closing_) {
                return;
            }
            closing_ = true;
            brls::Logger::info("login: logged in");
            std::function<void()> onDone = onDone_;
            brls::Application::popActivity(brls::TransitionAnimation::FADE, [onDone] {
                if (onDone) {
                    onDone();
                }
            });
        });
}

}  // namespace snx::ui
