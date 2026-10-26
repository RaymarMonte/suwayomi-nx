#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/ApiError.hpp"
#include "core/AppConfig.hpp"
#include "core/models/ServerInfo.hpp"

// The setup screen's state and rules (WP-08), without borealis so it is unit-tested: what the user typed, what is missing,
// which warnings apply, and how the connection test went. SetupActivity (the view) shows it and runs the test.
namespace snx::ui {

// Hints shown under the fields, most important first. The view turns each into a sentence (i18n "setup/warning/...").
enum class SetupWarning {
    TailscaleAddress,  // a *.ts.net name or a 100.64.0.0/10 address: the Switch has no Tailscale (D18), use the home address
    PasswordOverHttp,  // basic/ui_login over plain http: anyone on the same Wi-Fi could read the password
    PasswordOnSdCard,  // basic/ui_login: the password is saved as plain text on the SD card (D36)
    UseIpAddress,      // a name instead of an IP address: the Switch's name lookups are slow and block the network (D37)
};

// What stops a test from starting.
enum class SetupProblem { UrlMissing, UrlNotHttp, UsernameMissing, PasswordMissing };

// Pieces of a server address (pure, also used by the tests).
// The host of a normalized URL ("http://user@Host:4567/sub" -> "host"), lower case, without port or brackets.
std::string hostOf(std::string_view url);
bool isIpAddress(std::string_view host);         // dotted IPv4, or anything with ':' (IPv6)
bool isTailscaleHost(std::string_view host);     // "*.ts.net", or IPv4 100.64.0.0 - 100.127.255.255
bool isLocalHost(std::string_view host);         // "localhost" (the PC's own tests)

class SetupModel {
  public:
    enum class State {
        Editing,  // nothing tested yet, or changed since the last test
        Testing,  // the test runs
        Passed,   // the last test of exactly these settings worked
        Failed,   // it didn't: error() says why
    };

    // `start`: the saved settings (Settings -> Server) or the test config's; empty on the first run.
    explicit SetupModel(core::ServerSettings start = {});

    void setUrl(std::string typed);
    void setAuth(core::AuthMode auth);
    void setUsername(std::string username);
    void setPassword(std::string password);

    const core::ServerSettings& draft() const { return draft_; }
    bool needsCredentials() const { return draft_.auth != core::AuthMode::None; }

    std::vector<SetupWarning> warnings() const;
    std::optional<SetupProblem> problem() const;

    // What the client is configured with for the test: the URL normalized (api::normalizeServerUrl), and the tokens kept only
    // while the URL, login mode and username are the ones they were made for.
    core::ServerSettings settingsToTest() const;

    // The test. testStarted() returns false (and changes nothing) while problem() says something is missing or a test runs.
    bool testStarted();
    void testFinished(const api::ApiResult<core::ServerInfo>& result);
    State state() const { return state_; }
    const core::ServerInfo& server() const { return server_; }      // after Passed
    const api::ApiError& error() const { return error_; }           // after Failed
    std::optional<std::string> serverWarning() const;               // D9: after Passed, if the server is too old

    // "Continue" was pressed: true when it may go on at once (the current settings passed). Otherwise the view starts a
    // test, and continueAfterTest() says whether to go on when it passes.
    bool requestContinue();
    bool continueAfterTest() const { return continueWanted_; }

  private:
    void edited();

    core::ServerSettings start_;
    core::ServerSettings draft_;
    State state_ = State::Editing;
    core::ServerInfo server_;
    api::ApiError error_;
    bool continueWanted_ = false;
    int edits_ = 0;  // counts edits, so a test result for settings that changed meanwhile is dropped
    int testedEdits_ = 0;
};

}  // namespace snx::ui
