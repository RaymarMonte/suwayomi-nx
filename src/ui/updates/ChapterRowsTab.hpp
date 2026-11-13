#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "core/UpdatesHistory.hpp"
#include "ui/components/CoverGrid.hpp"
#include "ui/components/StatusView.hpp"
#include "ui/manga/MangaModel.hpp"
#include "ui/updates/ChapterRowsModel.hpp"
#include "ui/updates/LibraryUpdater.hpp"

// The Updates and History tabs (WP-15): one view for both, a one-column CoverGrid of rows (a small cover, the manga's
// title, the chapter and its page; read chapters dimmed), the day ("Today", "Yesterday", "Fri 13 Nov") on the first row
// of each day, or a status (loading, error + Retry, empty). Updates adds a header: the Update library / Stop button and a
// status line with a thin progress bar (LibraryUpdater).
// Controller (D16): A opens the chapter (Updates: that chapter; History: Continue, D43), X Updates: read / unread,
// History: refresh; Y Updates: the row menu, History: the details screen; MINUS the menu. Touch: a tap on the row opens
// the chapter, a tap on the cover the details screen, the row's "..." (Updates) the row menu, the header's button.
// The models live in MainActivity (D42); this view only shows them. Opening a chapter loads the manga and its whole
// chapter list first (MangaModel, maxChapters), so the reader knows the next and previous chapter (D44); the row says
// "Opening..." meanwhile; B cancels it. History adds "page N not saved yet" to a manga whose save waits in the queue
// (WP-15b, ui/reader/WaitingSaves).
namespace snx::ui {

class ChapterRowsTab : public brls::Box, private GridSource {
  public:
    // updater: the Updates tab's (nullptr for History). onRead: the reader opened from here closed (MainActivity
    // reloads both lists). onMangaChanged: a chapter action changed a manga's unread count (the library's badge).
    // The models are shared with MainActivity, so a server change while this tab shows can't pull them away under it.
    ChapterRowsTab(app::AppContext& context, std::shared_ptr<ChapterRowsModel> rows, std::shared_ptr<LibraryUpdater> updater,
                   std::function<void()> onRead, std::function<void(const core::Manga&)> onMangaChanged);
    ~ChapterRowsTab() override;

  private:
    // GridSource
    std::size_t count() const override;
    GridCell* makeCell() override;
    void bind(GridCell& cell, std::size_t index) override;
    void unbind(GridCell& cell) override;
    void select(std::size_t index) override;
    void focused(std::size_t index) override;
    void showing(std::size_t first, std::size_t last) override;

    bool isUpdates() const { return updater_ != nullptr; }
    void buildHeader();
    void modelChanged();
    void updaterChanged();
    void showGrid(bool visible);
    void openRow(std::size_t index);
    void openerChanged();
    void stopOpening();  // the open under way goes (B, a failure, the reader opening): the row shows its text again
    void openManga(std::size_t index);
    void openRowMenu(std::size_t index);
    void openMenu();
    void report();
    std::string dayText(std::size_t index) const;  // the day label for a row that starts a day, else ""
    const core::WaitingSave* waitingOf(std::size_t index) const;  // History: the row manga's waiting save, if any

    app::AppContext& context_;
    std::shared_ptr<ChapterRowsModel> rowsOwner_;
    std::shared_ptr<LibraryUpdater> updaterOwner_;
    ChapterRowsModel& rows_;
    LibraryUpdater* updater_;
    std::function<void()> onRead_;
    std::function<void(const core::Manga&)> onMangaChanged_;
    int updaterListener_ = 0;
    int savesListener_ = 0;  // History: on context_.saves, for the waiting note
    brls::Box* header_ = nullptr;
    brls::Button* updateButton_ = nullptr;
    brls::Label* statusLine_ = nullptr;
    brls::Box* barTrack_ = nullptr;
    brls::Box* barFill_ = nullptr;
    StatusView* status_;
    CoverGrid* grid_;
    bool gridShown_ = false;
    std::vector<core::DayGroup> groups_;
    std::int64_t offsetSeconds_ = 0;  // the console's time zone (platform::utcOffsetSeconds)
    std::int64_t today_ = 0;          // dayKey of now
    std::uint64_t shownList_ = 0;
    std::uint64_t shownItems_ = 0;
    std::size_t shownCount_ = 0;
    ChapterRowsModel::State shownState_ = ChapterRowsModel::State::Loading;
    std::string reported_;
    // Opening a chapter from a row.
    std::unique_ptr<MangaModel> opener_;
    std::size_t openingIndex_ = 0;
    int openingChapterId_ = 0;  // Updates: the row's chapter; History: 0 (Continue decides)
    std::uint64_t openStartMs_ = 0;
    std::uint64_t openTicket_ = 0;  // which open a posted step belongs to: one cancelled or replaced meanwhile does nothing
    // Set to false when this view goes: callbacks that arrive later (the reader's onClosed) check it.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace snx::ui
