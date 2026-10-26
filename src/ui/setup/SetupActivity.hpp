#pragma once

#include <borealis.hpp>
#include <functional>

#include "app/AppContext.hpp"
#include "net/CancelToken.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/setup/SetupModel.hpp"

// The setup screen (WP-08, ARCHITECTURE.md "ui"): server address, login mode, username and password, "Test connection"
// (shows the server's version or a clear error) and "Continue" (tests if needed, then saves and closes). It shows the
// warnings SetupModel finds (Tailscale address, password over http, ...). Opened by the main screen on the first run (no
// way back: there is no server yet), from Settings -> Server, and when basic auth is refused. Layout:
// resources/xml/activity/setup.xml. State and rules: SetupModel.
namespace snx::ui {

class SetupActivity : public brls::Activity {
  public:
    CONTENT_FROM_XML_RES("activity/setup.xml");

    // `start`: what the fields show. `canGoBack`: false on the first run (B and Cancel do nothing then). `onSaved` runs after
    // the settings were saved and this screen closed. `problem`: a message to show at once (why the screen opened).
    SetupActivity(app::AppContext& context, core::ServerSettings start, bool canGoBack, std::function<void()> onSaved,
                  std::string problem = {});
    ~SetupActivity() override;

    void onContentAvailable() override;

  private:
    void refresh();
    void startTest();
    void onTested(const api::ApiResult<core::ServerInfo>& result);
    void finish();
    void cancel();
    void showStatus(const std::string& text);

    BRLS_BIND(TextFieldCell, url, "setup/url");
    BRLS_BIND(brls::SelectorCell, auth, "setup/auth");
    BRLS_BIND(TextFieldCell, username, "setup/username");
    BRLS_BIND(TextFieldCell, password, "setup/password");
    BRLS_BIND(brls::Label, warnings, "setup/warnings");
    BRLS_BIND(brls::Label, status, "setup/status");
    BRLS_BIND(brls::Button, test, "setup/test");
    BRLS_BIND(brls::Button, next, "setup/continue");
    BRLS_BIND(brls::Button, back, "setup/cancel");
    BRLS_BIND(brls::Box, box, "setup/box");

    app::AppContext& context_;
    SetupModel model_;
    bool canGoBack_;
    std::function<void()> onSaved_;
    std::string problem_;
    net::CancelToken cancel_;  // the running test; cancelled when the screen goes away
    bool closing_ = false;
};

}  // namespace snx::ui
