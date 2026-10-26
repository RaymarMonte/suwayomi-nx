#pragma once

#include <borealis.hpp>
#include <nlohmann/json.hpp>

#include "app/AppContext.hpp"

// The Settings tab (WP-08): Server (address -> the setup screen, login mode, "Log out" for ui_login, the server's version)
// and About (app version, launch mode and profile, memory in use, safe mode, crash logs -> CrashLogsActivity).
// Layout: resources/xml/tabs/settings.xml.
namespace snx::ui {

class MainActivity;

class SettingsTab : public brls::Box {
  public:
    explicit SettingsTab(MainActivity& main);
    ~SettingsTab() override;

    // Shows the current server settings and connection again.
    void refresh();

    // What About shows, for the "screen" test event: mode, profile, memory, safe mode, crash logs.
    static nlohmann::ordered_json aboutParams(const app::AppContext& context);

  private:
    BRLS_BIND(brls::DetailCell, address, "settings/address");
    BRLS_BIND(brls::DetailCell, login, "settings/login");
    BRLS_BIND(brls::DetailCell, logout, "settings/logout");
    BRLS_BIND(brls::DetailCell, serverVersion, "settings/server_version");
    BRLS_BIND(brls::DetailCell, version, "settings/version");
    BRLS_BIND(brls::DetailCell, mode, "settings/mode");
    BRLS_BIND(brls::DetailCell, memory, "settings/memory");
    BRLS_BIND(brls::DetailCell, safeMode, "settings/safe_mode");
    BRLS_BIND(brls::DetailCell, crashLogs, "settings/crash_logs");

    MainActivity& main_;
};

// Settings -> About -> Crash logs: the saved crash records (logs/crashes/, newest first) with what each one says happened.
// Layout: resources/xml/activity/crash_logs.xml.
class CrashLogsActivity : public brls::Activity {
  public:
    CONTENT_FROM_XML_RES("activity/crash_logs.xml");

    explicit CrashLogsActivity(const platform::SafeFs& fs);
    void onContentAvailable() override;

    // The record files' names, newest first.
    static std::vector<std::string> recordNames(const platform::SafeFs& fs);

  private:
    BRLS_BIND(brls::Box, list, "crash_logs/list");

    const platform::SafeFs& fs_;
};

}  // namespace snx::ui
