#pragma once

#include <borealis.hpp>
#include <functional>

#include "app/AppContext.hpp"
#include "net/CancelToken.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/login/LoginModel.hpp"

// The login screen (WP-08, ui_login only): shown when the login couldn't be renewed (D38) and after "Log out" in Settings.
// Username and password, "Log in" (SuwayomiClient::login keeps the tokens; the app saves them through onSettingsChanged),
// and "Change server" (the setup screen). B does nothing: without a login the server answers nothing. Layout:
// resources/xml/activity/login.xml. State and rules: LoginModel.
namespace snx::ui {

class LoginActivity : public brls::Activity {
  public:
    CONTENT_FROM_XML_RES("activity/login.xml");

    // `onDone` runs after a successful login, once this screen has closed. `onChangeServer` opens the setup screen.
    // `message`: why the screen opened (empty after "Log out").
    LoginActivity(app::AppContext& context, std::function<void()> onDone, std::function<void()> onChangeServer,
                  std::string message = {});
    ~LoginActivity() override;

    void onContentAvailable() override;

  private:
    void startLogin();
    void showStatus(const std::string& text);

    BRLS_BIND(TextFieldCell, username, "login/username");
    BRLS_BIND(TextFieldCell, password, "login/password");
    BRLS_BIND(brls::Label, status, "login/status");
    BRLS_BIND(brls::Button, login, "login/login");
    BRLS_BIND(brls::Button, server, "login/server");
    BRLS_BIND(brls::Box, box, "login/box");

    app::AppContext& context_;
    LoginModel model_;
    std::function<void()> onDone_;
    std::function<void()> onChangeServer_;
    std::string message_;
    net::CancelToken cancel_;
    bool closing_ = false;
};

}  // namespace snx::ui
