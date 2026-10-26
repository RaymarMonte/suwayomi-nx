#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/ApiError.hpp"
#include "core/AppConfig.hpp"
#include "core/models/ServerInfo.hpp"

// The main screen's state (WP-08): its tabs, and the connection check it runs when it opens (and again after the server or
// login changed). Without borealis, so it is unit-tested. MainActivity (the view) shows it.
namespace snx::ui {

class MainModel {
  public:
    enum class Connection {
        NotSetUp,   // no server yet: the setup screen is on top
        Checking,   // asking the server for its version
        Connected,  // it answered: server() says which one
        Failed,     // no usable answer: error() says why, and the screen offers "Retry"
    };
    // What the view does after a check.
    enum class Next {
        Nothing,
        ShowLogin,  // ui_login and the login couldn't be renewed: the login screen
        ShowSetup,  // basic auth refused (wrong username or password): the setup screen
    };

    // The tabs in order (ARCHITECTURE.md "ui"). Their names are also the "screen" test events.
    static const std::vector<std::string_view>& tabs();

    explicit MainModel(bool serverSetUp);

    // A new check starts (the view has asked the server). False while one is still running.
    bool checkStarted();
    // Cancelled answers are ignored (Next::Nothing, state unchanged: a newer check or the screen's end cancelled it).
    Next checkFinished(const api::ApiResult<core::ServerInfo>& result, core::AuthMode auth);

    // The server or login was changed (setup or login screen): the next check is due.
    void serverChanged();

    Connection connection() const { return connection_; }
    const core::ServerInfo& server() const { return server_; }
    const api::ApiError& error() const { return error_; }
    std::optional<std::string> serverWarning() const;  // D9, while Connected

  private:
    Connection connection_;
    bool checkRunning_ = false;
    core::ServerInfo server_;
    api::ApiError error_;
};

}  // namespace snx::ui
