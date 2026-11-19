#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "core/SwitchDownloads.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/downloads/SwitchDownloads.hpp"

// The Switch downloads tab (WP-18, D59): chapters downloaded to this Switch's SD card. A header with the space ("2.3 GB of
// 8 GB used · 40 GB free on the SD card"), the queue's state ("Downloading · 3 in the queue", "Paused: 3 waiting", a
// Waiting reason, "Stopped: 2 failed") and the buttons Pause / Resume (or Retry failed) and Clear...; below it one list:
// the queue's entries (small cover, manga, chapter, "Queued" / "Downloading 40 %" / "Paused at 40 %" / "Failed at page 2 of
// 10", a thin bar), then "On this Switch" and one row per downloaded manga (cover, title, "3 chapters · 1.8 MB").
// Controller (D16): A / a tap on a queue row: its menu (Move to top, Move to bottom, Remove, Retry for a failed one, Open
// the manga); on a manga row: its details screen showing "Downloaded to Switch". Y: a queue row opens its manga, a manga row
// its menu (Open, Delete all downloads...). X Pause / Resume (Retry failed); MINUS the tab's menu.
// Keep awake (S9, owner OK 2026-11-19): the screen stays on while this tab is shown, nothing covers it, the app has focus
// and the queue runs. The model is the app's one (AppContext::switchDownloads): it downloads whether or not this tab is open.
namespace snx::ui {

class SwitchDownloadsTab : public brls::Box, private GridSource {
  public:
    SwitchDownloadsTab(app::AppContext& context, std::function<void(const core::Manga&)> onMangaChanged);
    ~SwitchDownloadsTab() override;

    // A screen is on top of the main screen (true) or closed again (false): the keep-awake follows.
    void setCovered(bool covered);

  private:
    struct Item {
        enum class Kind { Entry, Header, Manga } kind = Kind::Entry;
        core::SwitchQueueEntry entry;           // Entry
        SwitchDownloads::ChapterStatus status;  // Entry
        SwitchDownloads::MangaRow manga;        // Manga
    };

    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;

    void buildHeader();
    void changed(const SwitchDownloads::Change& change);
    void rebuildItems();
    void showGrid(bool visible);
    void updateHeader();
    void showButtons(bool shown);
    void updateKeepAwake();
    void primaryAction();  // X / the first button: Pause, Resume or Retry failed
    void askClear();
    void openEntryMenu(std::size_t index);
    void openMangaMenu(std::size_t index);
    void openMenu();
    void openManga(int mangaId, const std::string& title, const std::string& thumbnailUrl, bool onSwitch);
    void askDeleteManga(const SwitchDownloads::MangaRow& row);
    void yAction();

    app::AppContext& context_;
    SwitchDownloads& downloads_;
    std::function<void(const core::Manga&)> onMangaChanged_;
    int listener_ = 0;
    bool covered_ = false;
    bool awake_ = false;  // keepScreenOn(true) is ours now
    brls::Label* spaceLine_ = nullptr;
    brls::Label* stateLine_ = nullptr;
    brls::Button* primaryButton_ = nullptr;
    std::string primaryKey_;
    int buttonsShown_ = -1;
    brls::Button* clearButton_ = nullptr;
    StatusView* status_ = nullptr;
    CoverGrid* grid_ = nullptr;
    bool gridShown_ = false;
    std::vector<Item> items_;
};

}  // namespace snx::ui
