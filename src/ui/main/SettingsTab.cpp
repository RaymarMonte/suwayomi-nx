#include "core/SwitchDownloads.hpp"
#include "ui/main/SettingsTab.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "app/TestHooks.hpp"
#include "core/CrashRecord.hpp"
#include "core/Version.hpp"
#include "platform/Platform.hpp"
#include "ui/main/MainActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr const char* kCrashFolder = "logs/crashes";
constexpr std::size_t kMaxRecordBytes = 64 * 1024;

std::string authText(core::AuthMode mode) {
    switch (mode) {
        case core::AuthMode::None:
            return "setup/auth_none"_i18n;
        case core::AuthMode::Basic:
            return "setup/auth_basic"_i18n;
        case core::AuthMode::UiLogin:
            return "setup/auth_ui_login"_i18n;
    }
    return {};
}

std::string modeText(const app::AppContext& context) {
    const bool application = context.runtime.mode == core::LaunchMode::Application;
    return (application ? "settings/mode_application"_i18n : "settings/mode_applet"_i18n) + ", " +
           std::string(context.profile.name) + " " + "settings/profile"_i18n;
}

std::string memoryText() {
    const core::RuntimeInfo now = platform::runtimeInfo();
    constexpr std::uint64_t kMb = 1024 * 1024;
    return std::to_string(now.usedMem / kMb) + " MiB / " + std::to_string(now.totalMem / kMb) + " MiB";
}

}  // namespace

SettingsTab::SettingsTab(MainActivity& main) : main_(main) {
    inflateFromXMLRes("xml/tabs/settings.xml");
    app::AppContext& context = main_.context();

    address->registerClickAction([this](brls::View*) {
        main_.openSetup(false, {});
        return true;
    });
    logout->registerClickAction([this](brls::View*) {
        main_.logout();
        return true;
    });
    serverVersion->registerClickAction([this](brls::View*) {
        main_.retry();
        return true;
    });
    // D60: how long calls that make the server ask a source's website may take. Not chosen = the profile's (30 s).
    {
        const int chosenMs = core::sourceWaitMs(context.config.config().browse, context.profile.httpSourceTimeoutMs);
        std::vector<std::string> labels;
        int selected = 0;
        for (std::size_t i = 0; i < core::kSourceWaitChoices.size(); ++i) {
            const int seconds = core::kSourceWaitChoices[i];
            labels.push_back(seconds < 60    ? brls::getStr("settings/seconds", seconds)
                             : seconds == 60 ? "settings/one_minute"_i18n
                                             : brls::getStr("settings/minutes", seconds / 60));
            if (seconds * 1000 == chosenMs) {
                selected = static_cast<int>(i);
            }
        }
        sourceWait->init("settings/source_wait"_i18n, labels, selected, [&context](int index) {
            if (index < 0 || index >= static_cast<int>(core::kSourceWaitChoices.size())) {
                return;
            }
            const int seconds = core::kSourceWaitChoices[static_cast<std::size_t>(index)];
            core::BrowseSettings browse = context.config.config().browse;
            browse.sourceWaitSeconds = seconds;
            context.config.setBrowse(browse);
            context.client.setSourceTimeoutMs(seconds * 1000);
            app::testhooks::write("setting", nlohmann::ordered_json{{"name", "sourceWaitSeconds"}, {"value", seconds}});
        });
    }
    // WP-14 (D56): the add flow's category question; the picker's "Don't ask again" turns it off.
    askCategories->init("settings/ask_categories"_i18n, context.config.config().browse.askCategories, [&context](bool on) {
        core::BrowseSettings browse = context.config.config().browse;
        browse.askCategories = on;
        context.config.setBrowse(browse);
        app::testhooks::write("setting", nlohmann::ordered_json{{"name", "askCategories"}, {"value", on}});
    });
    // WP-18 (D59): the Switch downloads' space and their delete after reading (ReaderActivity, on closing).
    {
        const std::uint64_t chosen =
            core::switchLimitBytes(context.config.config().switchDownloads, context.profile.switchDownloadLimitBytes);
        std::vector<std::string> labels;
        int selected = 0;
        for (std::size_t i = 0; i < core::kSwitchLimitChoicesGb.size(); ++i) {
            const int gb = core::kSwitchLimitChoicesGb[i];
            labels.push_back(gb == 0 ? "settings/no_limit"_i18n : brls::getStr("settings/gigabytes", gb));
            if (static_cast<std::uint64_t>(gb) * core::kGiB == chosen) {
                selected = static_cast<int>(i);
            }
        }
        switchSpace->init("settings/switch_space"_i18n, labels, selected, [&context](int index) {
            if (index < 0 || index >= static_cast<int>(core::kSwitchLimitChoicesGb.size())) {
                return;
            }
            const int gb = core::kSwitchLimitChoicesGb[static_cast<std::size_t>(index)];
            core::SwitchDownloadSettings settings = context.config.config().switchDownloads;
            settings.limitGb = gb;
            context.config.setSwitchDownloads(settings);
            context.switchDownloads.setLimit(core::switchLimitBytes(settings, context.profile.switchDownloadLimitBytes));
            app::testhooks::write("setting", nlohmann::ordered_json{{"name", "switchLimitGb"}, {"value", gb}});
        });
    }
    switchDeleteAfterReading->init("settings/switch_delete_after_reading"_i18n,
                                   context.config.config().switchDownloads.deleteAfterReading, [&context](bool on) {
                                       core::SwitchDownloadSettings settings = context.config.config().switchDownloads;
                                       settings.deleteAfterReading = on;
                                       context.config.setSwitchDownloads(settings);
                                       app::testhooks::write("setting", nlohmann::ordered_json{
                                                                            {"name", "switchDeleteAfterReading"},
                                                                            {"value", on}});
                                   });
    // WP-17 (D58): a chapter finished in the reader loses its download on the server (ReaderActivity, on closing).
    deleteAfterReading->init("settings/delete_after_reading"_i18n,
                             context.config.config().serverDownloads.deleteAfterReading, [&context](bool on) {
                                 core::ServerDownloadSettings settings = context.config.config().serverDownloads;
                                 settings.deleteAfterReading = on;
                                 context.config.setServerDownloads(settings);
                                 app::testhooks::write("setting",
                                                       nlohmann::ordered_json{{"name", "deleteAfterReading"}, {"value", on}});
                             });
    version->setDetailText(core::versionLabel());
    mode->setDetailText(modeText(context));
    memory->setDetailText(memoryText());
    memory->registerClickAction([this](brls::View*) {
        memory->setDetailText(memoryText());  // tap for a fresh reading
        return true;
    });
    safeMode->setDetailText(context.lastRun.crashed ? "settings/safe_on"_i18n : "settings/safe_off"_i18n);
    const std::size_t records = CrashLogsActivity::recordNames(context.fs).size();
    crashLogs->setDetailText(records == 0 ? "settings/crash_logs_none"_i18n : std::to_string(records));
    crashLogs->registerClickAction([&context](brls::View*) {
        brls::Application::pushActivity(new CrashLogsActivity(context.fs));
        return true;
    });
    refresh();
}

SettingsTab::~SettingsTab() {
    main_.forgetTab(this);
}

void SettingsTab::refresh() {
    const core::ServerSettings server = main_.context().client.settings();
    address->setDetailText(server.url.empty() ? "main/not_set_up"_i18n : server.url);
    std::string loginText = authText(server.auth);
    if (server.auth != core::AuthMode::None && !server.username.empty()) {
        loginText += " (" + server.username + ")";
    }
    login->setDetailText(loginText);
    logout->setVisibility(server.auth == core::AuthMode::UiLogin ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    switch (main_.model().connection()) {
        case MainModel::Connection::Connected:
            serverVersion->setDetailText(main_.model().server().version);
            break;
        case MainModel::Connection::Failed:
            // WP-18b: offline (the downloaded chapters still read) or another failure
            serverVersion->setDetailText(main_.context().connection.offline() ? "settings/server_offline"_i18n
                                                                              : "settings/unreachable"_i18n);
            break;
        default:
            serverVersion->setDetailText("settings/checking"_i18n);
            break;
    }
}

nlohmann::ordered_json SettingsTab::aboutParams(const app::AppContext& context) {
    const core::RuntimeInfo now = platform::runtimeInfo();
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["mode"] = std::string(core::launchModeName(context.runtime.mode));
    params["profile"] = std::string(context.profile.name);
    params["memUsed"] = now.usedMem;
    params["memTotal"] = now.totalMem;
    params["safeMode"] = context.lastRun.crashed;
    params["crashLogs"] = CrashLogsActivity::recordNames(context.fs).size();
    return params;
}

CrashLogsActivity::CrashLogsActivity(const platform::SafeFs& fs) : fs_(fs) {}

std::vector<std::string> CrashLogsActivity::recordNames(const platform::SafeFs& fs) {
    std::vector<std::string> names;
    for (const std::string& name : fs.listDir(kCrashFolder)) {
        if (core::crashrecord::isCrashFileName(name)) {
            names.push_back(name);
        }
    }
    std::sort(names.rbegin(), names.rend());  // the names hold the date and time: newest first
    return names;
}

void CrashLogsActivity::onContentAvailable() {
    const std::vector<std::string> names = recordNames(fs_);
    for (const std::string& name : names) {
        auto* cell = new brls::DetailCell();
        cell->setText(name);
        std::string what = "?";
        if (const auto text = fs_.readFile(std::string(kCrashFolder) + "/" + name, kMaxRecordBytes)) {
            if (const auto summary = core::crashrecord::parseCrashRecord(*text)) {
                what = summary->type;
            }
        }
        cell->setDetailText(what);
        list->addView(cell);
    }
    if (names.empty()) {
        auto* empty = new brls::Label();
        empty->setText("crash_logs/empty"_i18n);
        empty->setFontSize(20);
        list->addView(empty);
        list->setFocusable(true);  // so B works with nothing else to focus
        list->setHideHighlight(true);
    }
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["count"] = names.size();
    app::testhooks::screen("CrashLogs", params);
}

}  // namespace snx::ui
