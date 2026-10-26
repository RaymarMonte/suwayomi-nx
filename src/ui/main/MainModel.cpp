#include "ui/main/MainModel.hpp"

#include "api/SuwayomiClient.hpp"

namespace snx::ui {

const std::vector<std::string_view>& MainModel::tabs() {
    static const std::vector<std::string_view> kTabs = {"Library", "Updates", "History", "Browse", "Settings"};
    return kTabs;
}

MainModel::MainModel(bool serverSetUp) : connection_(serverSetUp ? Connection::Checking : Connection::NotSetUp) {}

bool MainModel::checkStarted() {
    if (checkRunning_) {
        return false;
    }
    checkRunning_ = true;
    connection_ = Connection::Checking;
    return true;
}

MainModel::Next MainModel::checkFinished(const api::ApiResult<core::ServerInfo>& result, core::AuthMode auth) {
    if (!result.ok() && result.error().kind == api::ApiErrorKind::Cancelled) {
        return Next::Nothing;
    }
    checkRunning_ = false;
    if (result.ok()) {
        connection_ = Connection::Connected;
        server_ = result.value();
        return Next::Nothing;
    }
    connection_ = Connection::Failed;
    error_ = result.error();
    if (error_.kind == api::ApiErrorKind::Auth) {
        if (auth == core::AuthMode::UiLogin) {
            return Next::ShowLogin;
        }
        if (auth == core::AuthMode::Basic) {
            return Next::ShowSetup;
        }
    }
    return Next::Nothing;
}

void MainModel::serverChanged() {
    connection_ = Connection::Checking;
    checkRunning_ = false;  // a check still under way is for the old settings: the view cancels it
}

std::optional<std::string> MainModel::serverWarning() const {
    if (connection_ != Connection::Connected) {
        return std::nullopt;
    }
    return api::serverWarning(server_);
}

}  // namespace snx::ui
