#include "ui/setup/SetupModel.hpp"

#include <cctype>
#include <utility>

#include "api/ServerUrl.hpp"
#include "api/SuwayomiClient.hpp"

namespace snx::ui {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool endsWith(std::string_view text, std::string_view tail) {
    return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail;
}

// "100.64.1.2" -> {100, 64, 1, 2}; false if it isn't four numbers 0..255.
bool ipv4Parts(std::string_view host, int parts[4]) {
    int index = 0;
    int value = -1;
    for (const char c : host) {
        if (c >= '0' && c <= '9') {
            value = (value < 0 ? 0 : value * 10) + (c - '0');
            if (value > 255) {
                return false;
            }
        } else if (c == '.' && value >= 0 && index < 3) {
            parts[index++] = value;
            value = -1;
        } else {
            return false;
        }
    }
    if (value < 0 || index != 3) {
        return false;
    }
    parts[3] = value;
    return true;
}

}  // namespace

std::string hostOf(std::string_view url) {
    std::string_view rest = url;
    if (const auto scheme = rest.find("://"); scheme != std::string_view::npos) {
        rest.remove_prefix(scheme + 3);
    }
    rest = rest.substr(0, rest.find_first_of("/?#"));
    if (const auto at = rest.rfind('@'); at != std::string_view::npos) {
        rest.remove_prefix(at + 1);
    }
    if (!rest.empty() && rest.front() == '[') {  // [::1]:4567
        const auto close = rest.find(']');
        return lower(rest.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1));
    }
    return lower(rest.substr(0, rest.find(':')));
}

bool isIpAddress(std::string_view host) {
    int parts[4];
    return ipv4Parts(host, parts) || host.find(':') != std::string_view::npos;
}

bool isTailscaleHost(std::string_view host) {
    const std::string name = lower(host);
    if (name == "ts.net" || endsWith(name, ".ts.net")) {
        return true;
    }
    int parts[4];
    // 100.64.0.0/10: the first byte 100, the second 64..127 (Tailscale's CGNAT range).
    return ipv4Parts(name, parts) && parts[0] == 100 && parts[1] >= 64 && parts[1] <= 127;
}

bool isLocalHost(std::string_view host) {
    return lower(host) == "localhost";
}

SetupModel::SetupModel(core::ServerSettings start) : start_(std::move(start)), draft_(start_) {}

void SetupModel::edited() {
    ++edits_;
    if (state_ != State::Testing) {
        state_ = State::Editing;
    }
    continueWanted_ = false;
}

void SetupModel::setUrl(std::string typed) {
    draft_.url = std::move(typed);
    edited();
}

void SetupModel::setAuth(core::AuthMode auth) {
    draft_.auth = auth;
    edited();
}

void SetupModel::setUsername(std::string username) {
    draft_.username = std::move(username);
    edited();
}

void SetupModel::setPassword(std::string password) {
    draft_.password = std::move(password);
    edited();
}

std::vector<SetupWarning> SetupModel::warnings() const {
    std::vector<SetupWarning> out;
    const std::string url = api::normalizeServerUrl(draft_.url);
    if (url.empty()) {
        return out;
    }
    const std::string host = hostOf(url);
    if (isTailscaleHost(host)) {
        out.push_back(SetupWarning::TailscaleAddress);
    }
    if (needsCredentials()) {
        if (url.rfind("http://", 0) == 0) {
            out.push_back(SetupWarning::PasswordOverHttp);
        }
        out.push_back(SetupWarning::PasswordOnSdCard);
    }
    if (!host.empty() && !isIpAddress(host) && !isTailscaleHost(host) && !isLocalHost(host)) {
        out.push_back(SetupWarning::UseIpAddress);
    }
    return out;
}

std::optional<SetupProblem> SetupModel::problem() const {
    const std::string url = api::normalizeServerUrl(draft_.url);
    if (url.empty() || hostOf(url).empty()) {
        return SetupProblem::UrlMissing;
    }
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        return SetupProblem::UrlNotHttp;
    }
    if (needsCredentials() && draft_.username.empty()) {
        return SetupProblem::UsernameMissing;
    }
    if (needsCredentials() && draft_.password.empty()) {
        return SetupProblem::PasswordMissing;
    }
    return std::nullopt;
}

core::ServerSettings SetupModel::settingsToTest() const {
    core::ServerSettings settings = draft_;
    settings.url = api::normalizeServerUrl(draft_.url);
    const bool sameLogin = settings.url == api::normalizeServerUrl(start_.url) && settings.auth == start_.auth &&
                           settings.username == start_.username;
    if (!sameLogin || settings.auth != core::AuthMode::UiLogin) {
        settings.accessToken.clear();
        settings.refreshToken.clear();
    }
    return settings;
}

bool SetupModel::testStarted() {
    if (state_ == State::Testing || problem()) {
        return false;
    }
    state_ = State::Testing;
    testedEdits_ = edits_;
    return true;
}

void SetupModel::testFinished(const api::ApiResult<core::ServerInfo>& result) {
    if (edits_ != testedEdits_) {
        state_ = State::Editing;  // the answer is for settings the user has changed since
        continueWanted_ = false;
        return;
    }
    if (result.ok()) {
        state_ = State::Passed;
        server_ = result.value();
    } else {
        state_ = State::Failed;
        error_ = result.error();
        continueWanted_ = false;
    }
}

std::optional<std::string> SetupModel::serverWarning() const {
    if (state_ != State::Passed) {
        return std::nullopt;
    }
    return api::serverWarning(server_);
}

bool SetupModel::requestContinue() {
    if (state_ == State::Passed) {
        return true;
    }
    continueWanted_ = true;
    return false;
}

}  // namespace snx::ui
