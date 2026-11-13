#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "app/AppContext.hpp"
#include "net/CancelToken.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/library/LibraryModel.hpp"
#include "ui/main/MainModel.hpp"
#include "ui/updates/ChapterRowsModel.hpp"
#include "ui/updates/LibraryUpdater.hpp"

// The main screen (WP-08): the tab frame every later screen hangs off (Library, Updates, History, Browse, Settings; Updates and
// History since WP-15; Browse is a placeholder until its WP). It is always the first screen, so it is never closed: setup and
// login open on top of it. When it opens (and after the server or login changed) it asks the server for its version; until
// that works the Library tab shows the check (a "Retry" on errors), and a refused login opens the login (ui_login) or setup
// (basic) screen. Once connected it keeps the library's model (WP-09), so the library survives tab changes.
// Layout: resources/xml/activity/main.xml (the tabs are added in code). State and rules: MainModel.
namespace snx::ui {

class LibraryTab;
class SettingsTab;

class MainActivity : public brls::Activity {
  public:
    CONTENT_FROM_XML_RES("activity/main.xml");

    explicit MainActivity(app::AppContext& context);
    ~MainActivity() override;

    void onContentAvailable() override;
    void onResume() override;  // a screen on top of it closed

    // For the tabs.
    app::AppContext& context() { return context_; }
    const MainModel& model() const { return model_; }
    void openSetup(bool firstRun, std::string problem);
    void openLogin(std::string message);
    void logout();
    void retry();
    void forgetTab(const brls::View* tab);  // a tab's view is being deleted (the frame deletes it when another tab opens)

  private:
    brls::View* makeTab(std::size_t index);
    void startCheck();
    void onCheck(const api::ApiResult<core::ServerInfo>& result);
    void showConnection();
    void serverChanged();
    void dropLibrary();
    brls::View* makeRowsTab(bool updates);  // WP-15: Updates or History
    void ensureLists();
    void listsRead();             // the reader opened from Updates or History closed
    void reloadStaleLists();      // after a screen on top closed: the lists that loaded once load again
    void updaterChanged();        // the library update moved on: events, reloads
    void waitingSaveDone(const core::WaitingSave& entry);  // the queue sent or dropped a save: lists reload (WP-15b)
    void mangaChanged(const core::Manga& manga);
    void reportScreen();

    BRLS_BIND(brls::TabFrame, tabs, "main/tabs");

    app::AppContext& context_;
    MainModel model_;
    net::CancelToken cancel_;  // the running check
    std::size_t currentTab_ = 0;
    std::unique_ptr<LibraryModel> library_;  // made once connected; dropped when the server or login changes
    // WP-15: the Updates and History rows and the library update, made when their tab first opens once connected,
    // dropped with the library. Shared with the tab's view (it may outlive a drop until the frame deletes it).
    std::shared_ptr<ChapterRowsModel> updates_;
    std::shared_ptr<ChapterRowsModel> history_;
    std::shared_ptr<LibraryUpdater> updater_;
    int updaterListener_ = 0;
    int savesListener_ = 0;  // on context_.saves (WP-15b)
    std::uint64_t updaterFinished_ = 0;  // the updater's finishedCount() already handled
    std::string updaterReported_;        // the last library_update event, so a poll with no news writes none
    LibraryTab* libraryView_ = nullptr;  // the Library tab's view while it exists
    SettingsTab* settingsView_ = nullptr;
    bool covered_ = false;  // setup or login is on top: tab changes underneath aren't reported as screens
    bool listsStale_ = false;  // a screen on top closed since the lists last reloaded (reloadStaleLists)
};

}  // namespace snx::ui
