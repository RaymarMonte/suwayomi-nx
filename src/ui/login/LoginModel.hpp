#pragma once

#include <optional>
#include <string>

#include "api/ApiError.hpp"

// The login screen's state (WP-08, ui_login only): the username and password, and how the login went. Without borealis, so
// it is unit-tested. LoginActivity (the view) shows it and calls SuwayomiClient::login.
namespace snx::ui {

class LoginModel {
  public:
    enum class State { Editing, LoggingIn, Failed, Done };
    enum class Problem { UsernameMissing, PasswordMissing };

    explicit LoginModel(std::string username = {}, std::string password = {});

    void setUsername(std::string username);
    void setPassword(std::string password);
    const std::string& username() const { return username_; }
    const std::string& password() const { return password_; }

    std::optional<Problem> problem() const;

    // False (and nothing changes) while a login runs or something is missing.
    bool loginStarted();
    // Cancelled answers (the screen went away) are ignored. A failure keeps what was typed, so only the wrong part needs fixing.
    void loginFinished(const api::ApiResult<bool>& result);

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }

  private:
    std::string username_;
    std::string password_;
    State state_ = State::Editing;
    api::ApiError error_;
};

}  // namespace snx::ui
