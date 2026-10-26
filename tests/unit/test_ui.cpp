// WP-08: the screens' models (src/ui/<screen>/<Name>Model): setup, login and the main screen's connection check.

#include <algorithm>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "api/ApiError.hpp"
#include "ui/login/LoginModel.hpp"
#include "ui/main/MainModel.hpp"
#include "ui/setup/SetupModel.hpp"

using namespace snx;
using snx::ui::SetupModel;
using snx::ui::SetupProblem;
using snx::ui::SetupWarning;

namespace {

bool has(const std::vector<SetupWarning>& list, SetupWarning w) {
    return std::find(list.begin(), list.end(), w) != list.end();
}

core::ServerInfo info(std::string version) {
    core::ServerInfo i;
    i.name = "Suwayomi-Server";
    i.version = std::move(version);
    i.buildType = "Stable";
    return i;
}

api::ApiResult<core::ServerInfo> failed(api::ApiErrorKind kind) {
    return api::ApiResult<core::ServerInfo>(api::makeApiError(kind, "detail"));
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("setup: hosts, IP addresses, Tailscale names and ranges") {
    CHECK(ui::hostOf("http://192.168.1.10:4567") == "192.168.1.10");
    CHECK(ui::hostOf("https://User@NAS.local:4567/suwayomi?x=1") == "nas.local");
    CHECK(ui::hostOf("http://[fd00::1]:4567/") == "fd00::1");
    CHECK(ui::hostOf("http://box") == "box");
    CHECK(ui::hostOf("") == "");

    CHECK(ui::isIpAddress("192.168.1.10"));
    CHECK(ui::isIpAddress("fd00::1"));
    CHECK_FALSE(ui::isIpAddress("256.1.1.1"));
    CHECK_FALSE(ui::isIpAddress("1.2.3"));
    CHECK_FALSE(ui::isIpAddress("nas.local"));

    CHECK(ui::isTailscaleHost("box.tail1234.ts.net"));
    CHECK(ui::isTailscaleHost("BOX.TAIL1234.TS.NET"));
    CHECK(ui::isTailscaleHost("100.64.0.1"));
    CHECK(ui::isTailscaleHost("100.127.255.255"));
    CHECK_FALSE(ui::isTailscaleHost("100.63.255.255"));
    CHECK_FALSE(ui::isTailscaleHost("100.128.0.1"));
    CHECK_FALSE(ui::isTailscaleHost("notts.net"));
    CHECK_FALSE(ui::isTailscaleHost("192.168.1.10"));
}

TEST_CASE("setup: what is missing before a test") {
    SetupModel model;
    CHECK(model.problem() == SetupProblem::UrlMissing);
    CHECK_FALSE(model.testStarted());
    model.setUrl("ftp://box");
    CHECK(model.problem() == SetupProblem::UrlNotHttp);
    model.setUrl(" 192.168.1.10:4567/ ");
    CHECK_FALSE(model.problem().has_value());
    CHECK(model.settingsToTest().url == "http://192.168.1.10:4567");
    model.setAuth(core::AuthMode::Basic);
    CHECK(model.problem() == SetupProblem::UsernameMissing);
    model.setUsername("snx");
    CHECK(model.problem() == SetupProblem::PasswordMissing);
    model.setPassword("pw");
    CHECK_FALSE(model.problem().has_value());
}

TEST_CASE("setup: warnings") {
    SetupModel model;
    CHECK(model.warnings().empty());
    model.setUrl("https://box.tail1234.ts.net:4567");
    auto w = model.warnings();
    CHECK(has(w, SetupWarning::TailscaleAddress));
    CHECK_FALSE(has(w, SetupWarning::UseIpAddress));  // the Tailscale warning says it better
    CHECK_FALSE(has(w, SetupWarning::PasswordOverHttp));

    model.setUrl("http://100.101.102.103:4567");
    CHECK(has(model.warnings(), SetupWarning::TailscaleAddress));

    model.setUrl("http://192.168.1.10:4567");
    CHECK(model.warnings().empty());
    model.setAuth(core::AuthMode::Basic);
    w = model.warnings();
    CHECK(has(w, SetupWarning::PasswordOverHttp));
    CHECK(has(w, SetupWarning::PasswordOnSdCard));

    model.setUrl("https://nas.local");
    w = model.warnings();
    CHECK_FALSE(has(w, SetupWarning::PasswordOverHttp));
    CHECK(has(w, SetupWarning::PasswordOnSdCard));
    CHECK(has(w, SetupWarning::UseIpAddress));

    model.setAuth(core::AuthMode::None);
    model.setUrl("http://localhost:4568");
    CHECK(model.warnings().empty());
}

TEST_CASE("setup: test, then continue; an edit makes the result stale") {
    SetupModel model;
    model.setUrl("http://10.0.0.2:4568");
    CHECK_FALSE(model.requestContinue());  // not tested yet: the view starts a test
    CHECK(model.continueAfterTest());
    REQUIRE(model.testStarted());
    CHECK_FALSE(model.testStarted());  // one at a time
    CHECK(model.state() == SetupModel::State::Testing);
    model.testFinished(info("v2.4.2366"));
    CHECK(model.state() == SetupModel::State::Passed);
    CHECK(model.server().version == "v2.4.2366");
    CHECK_FALSE(model.serverWarning().has_value());
    CHECK(model.requestContinue());

    // An edit while testing drops that test's answer.
    model.setUrl("http://10.0.0.3:4568");
    CHECK(model.state() == SetupModel::State::Editing);
    REQUIRE(model.testStarted());
    model.setUrl("http://10.0.0.4:4568");
    model.testFinished(info("v2.4.2366"));
    CHECK(model.state() == SetupModel::State::Editing);

    REQUIRE(model.testStarted());
    model.testFinished(failed(api::ApiErrorKind::Offline));
    CHECK(model.state() == SetupModel::State::Failed);
    CHECK(model.error().kind == api::ApiErrorKind::Offline);
    CHECK_FALSE(model.continueAfterTest());

    REQUIRE(model.testStarted());
    model.testFinished(info("v1.0.0"));
    CHECK(model.serverWarning().has_value());  // D9: too old
}

TEST_CASE("setup: ui_login tokens are kept only for the same server and user") {
    core::ServerSettings saved;
    saved.url = "http://10.0.0.2:4568";
    saved.auth = core::AuthMode::UiLogin;
    saved.username = "snx";
    saved.password = "pw";
    saved.accessToken = "a";
    saved.refreshToken = "r";
    SetupModel model(saved);
    CHECK(model.settingsToTest().refreshToken == "r");
    model.setUsername("other");
    CHECK(model.settingsToTest().refreshToken.empty());
    model.setUsername("snx");
    model.setUrl("http://10.0.0.9:4568");
    CHECK(model.settingsToTest().accessToken.empty());
    model.setUrl("10.0.0.2:4568");  // the same server, typed differently
    CHECK(model.settingsToTest().accessToken == "a");
    model.setAuth(core::AuthMode::Basic);
    CHECK(model.settingsToTest().accessToken.empty());
}

TEST_CASE("login: fields, one login at a time, failures keep what was typed") {
    ui::LoginModel model("snx");
    CHECK(model.problem() == ui::LoginModel::Problem::PasswordMissing);
    CHECK_FALSE(model.loginStarted());
    model.setPassword("wrong");
    REQUIRE(model.loginStarted());
    CHECK_FALSE(model.loginStarted());
    model.loginFinished(api::ApiResult<bool>(api::makeApiError(api::ApiErrorKind::Cancelled)));
    CHECK(model.state() == ui::LoginModel::State::LoggingIn);  // ignored
    model.loginFinished(api::ApiResult<bool>(api::makeApiError(api::ApiErrorKind::Auth, "bad password")));
    CHECK(model.state() == ui::LoginModel::State::Failed);
    CHECK(model.error().kind == api::ApiErrorKind::Auth);
    CHECK(model.username() == "snx");
    model.setPassword("right");
    CHECK(model.state() == ui::LoginModel::State::Editing);
    REQUIRE(model.loginStarted());
    model.loginFinished(api::ApiResult<bool>(true));
    CHECK(model.state() == ui::LoginModel::State::Done);

    ui::LoginModel empty;
    CHECK(empty.problem() == ui::LoginModel::Problem::UsernameMissing);
}

TEST_CASE("main: tabs and the connection check") {
    CHECK(ui::MainModel::tabs().front() == "Library");
    CHECK(ui::MainModel::tabs().back() == "Settings");
    CHECK(ui::MainModel::tabs().size() == 5);

    ui::MainModel notSetUp(false);
    CHECK(notSetUp.connection() == ui::MainModel::Connection::NotSetUp);

    ui::MainModel model(true);
    CHECK(model.connection() == ui::MainModel::Connection::Checking);
    REQUIRE(model.checkStarted());
    CHECK_FALSE(model.checkStarted());
    CHECK(model.checkFinished(failed(api::ApiErrorKind::Cancelled), core::AuthMode::None) == ui::MainModel::Next::Nothing);
    CHECK(model.connection() == ui::MainModel::Connection::Checking);
    CHECK(model.checkFinished(info("v2.4.2366"), core::AuthMode::None) == ui::MainModel::Next::Nothing);
    CHECK(model.connection() == ui::MainModel::Connection::Connected);
    CHECK(model.server().version == "v2.4.2366");
    CHECK_FALSE(model.serverWarning().has_value());

    REQUIRE(model.checkStarted());
    CHECK(model.checkFinished(failed(api::ApiErrorKind::Offline), core::AuthMode::UiLogin) == ui::MainModel::Next::Nothing);
    CHECK(model.connection() == ui::MainModel::Connection::Failed);
    CHECK(model.error().kind == api::ApiErrorKind::Offline);

    REQUIRE(model.checkStarted());
    CHECK(model.checkFinished(failed(api::ApiErrorKind::Auth), core::AuthMode::UiLogin) == ui::MainModel::Next::ShowLogin);
    REQUIRE(model.checkStarted());
    CHECK(model.checkFinished(failed(api::ApiErrorKind::Auth), core::AuthMode::Basic) == ui::MainModel::Next::ShowSetup);
    REQUIRE(model.checkStarted());
    CHECK(model.checkFinished(failed(api::ApiErrorKind::Auth), core::AuthMode::None) == ui::MainModel::Next::Nothing);

    model.serverChanged();
    CHECK(model.connection() == ui::MainModel::Connection::Checking);
    CHECK(model.checkStarted());
    CHECK(model.checkFinished(info("v1.0.0"), core::AuthMode::None) == ui::MainModel::Next::Nothing);
    CHECK(model.serverWarning().has_value());
}

}  // TEST_SUITE
