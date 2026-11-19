#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "core/ServerDownloads.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/downloads/ServerDownloads.hpp"

// The Server downloads tab (WP-17, D58): the server's download queue (downloads onto the server's own disk, nothing on the
// Switch). A header with the state ("Downloading · 14 in the queue", "Starting...", "Paused: 14 waiting", "Stopped: 2
// failed", "Can't reach the server") and the buttons Pause / Resume (or Retry failed) and Clear...; below it a one-column
// CoverGrid of rows in the server's order (small cover, manga, chapter, "Queued" / "Downloading 40 %" / "Paused at 66 %" /
// "Failed after 3 tries", a thin progress bar), or a status when nothing is queued.
// Controller (D16): A / a tap on a row: its menu (Move to top, Move to bottom, Remove, Retry for a failed row, Open the
// manga); X Pause / Resume (Retry failed when only failed rows wait); Y opens the manga; MINUS the tab's menu (Pause /
// Resume, Retry failed, Clear queue..., Refresh). Every action shows the server's answer (no guessing ahead).
// The queue model is the app's one (AppContext::downloads); this view watches it while it is shown and the main screen is
// on top (setCovered), so it polls only then.
namespace snx::ui {

class ServerDownloadsTab : public brls::Box, private GridSource {
  public:
    // onMangaChanged: passed to a details screen opened from here (the library's badge).
    ServerDownloadsTab(app::AppContext& context, std::function<void(const core::Manga&)> onMangaChanged);
    ~ServerDownloadsTab() override;

    // A screen is on top of the main screen (true) or closed again (false): stop or start watching the queue.
    void setCovered(bool covered);

  private:
    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;

    void buildHeader();
    void queueChanged(const ServerDownloads::Change& change);
    void showGrid(bool visible);
    void updateHeader();
    void showButtons(bool shown);
    void primaryAction();  // X / the first button: Pause, Resume or Retry failed
    void askClear();
    void openRowMenu(std::size_t index);
    void openMenu();
    void openManga(std::size_t index);
    void report(double applyMs);

    app::AppContext& context_;
    ServerDownloads& downloads_;
    std::function<void(const core::Manga&)> onMangaChanged_;
    int listener_ = 0;
    int watcher_ = 0;  // 0 = not watching
    bool covered_ = false;
    brls::Label* statusLine_ = nullptr;
    brls::Label* hintLine_ = nullptr;
    brls::Button* primaryButton_ = nullptr;
    std::string primaryKey_;  // the first button's text key now (the footer's X hint follows it)
    int buttonsShown_ = -1;   // 1: the header's buttons and X / Y show (the queue has rows); 0: hidden; -1: not set yet
    brls::Button* clearButton_ = nullptr;
    StatusView* status_ = nullptr;
    CoverGrid* grid_ = nullptr;
    bool gridShown_ = false;
    std::vector<core::DownloadRow> rows_;  // what the grid shows: the queue's rows at the last change
    std::string reported_;
};

}  // namespace snx::ui
