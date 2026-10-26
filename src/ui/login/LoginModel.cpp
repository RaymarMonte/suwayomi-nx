#include "ui/login/LoginModel.hpp"

#include <utility>

namespace snx::ui {

LoginModel::LoginModel(std::string username, std::string password)
    : username_(std::move(username)), password_(std::move(password)) {}

void LoginModel::setUsername(std::string username) {
    username_ = std::move(username);
    if (state_ != State::LoggingIn) {
        state_ = State::Editing;
    }
}

void LoginModel::setPassword(std::string password) {
    password_ = std::move(password);
    if (state_ != State::LoggingIn) {
        state_ = State::Editing;
    }
}

std::optional<LoginModel::Problem> LoginModel::problem() const {
    if (username_.empty()) {
        return Problem::UsernameMissing;
    }
    if (password_.empty()) {
        return Problem::PasswordMissing;
    }
    return std::nullopt;
}

bool LoginModel::loginStarted() {
    if (state_ == State::LoggingIn || problem()) {
        return false;
    }
    state_ = State::LoggingIn;
    return true;
}

void LoginModel::loginFinished(const api::ApiResult<bool>& result) {
    if (!result.ok() && result.error().kind == api::ApiErrorKind::Cancelled) {
        return;
    }
    if (result.ok()) {
        state_ = State::Done;
    } else {
        state_ = State::Failed;
        error_ = result.error();
    }
}

}  // namespace snx::ui
