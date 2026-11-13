#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/UpdatesHistory.hpp"
#include "core/models/ChapterEntry.hpp"
#include "net/CancelToken.hpp"

// The rows of the Updates or the History tab (WP-15), without borealis so it is unit-tested (FakeHttpClient).
// MainActivity owns one of each (like the library, D42: the tab frame deletes a tab's view when another tab opens).
//
// Updates: the library's chapters, most recently found first, only the new ones (core::filterUpdates: fetched after the
// manga joined the library, within 90 days of the newest). History: chapters read or with progress, most recently read
// first, one row per manga (core::historyRows), of any manga.
//
// Loading: page after page (RuntimeProfile::updatesPageSize / historyPageSize per request). Pages whose chapters the rule
// hides add few rows, so after each page the next one loads by itself while there is less than a screen of rows more
// than when this run started, at most `fillPages` pages in a row; then it waits until the list shows near its end
// (showing()). The list ends at the window (Updates), the server's last page or `maxRows` (S6).
namespace snx::ui {

class ChapterRowsModel {
  public:
    enum class Kind { Updates, History };
    enum class State {
        Loading,  // the first page is on its way (and no older list is shown)
        Ready,    // items() has rows; more may follow
        Empty,    // no rows (Updates: no new chapters in the window; History: nothing read)
        Failed,   // nothing to show: error() says why
    };
    struct Limits {
        int fillPages = 5;         // RuntimeProfile::updatesFillPages
        std::size_t maxRows = 0;   // RuntimeProfile::maxUpdateRows / maxHistoryRows
        std::size_t screenRows = 8;  // rows on one screen (the view's height / row height)
    };

    ChapterRowsModel(api::SuwayomiClient& client, Kind kind, Limits limits);
    ~ChapterRowsModel();  // cancels what is under way
    ChapterRowsModel(const ChapterRowsModel&) = delete;
    ChapterRowsModel& operator=(const ChapterRowsModel&) = delete;

    // Called after every change of what the tab shows (through the client's deliver: the main thread in the app).
    void setListener(std::function<void()> listener);
    // A chapter action changed a manga: its unread count, from manga(id) after the action (the library's badge follows).
    void setMangaChanged(std::function<void(const core::Manga&)> changed);

    void start();   // the first page. Only once; later calls do nothing.
    // The list again from the first page (after reading, after a library update, Refresh). The rows on screen stay until
    // the new first page is in; if that fails, they stay and takeError() says why.
    void reload();
    // The view shows rows up to `lastIndex`: near the end of what is loaded, the next pages are asked for.
    void showing(std::size_t lastIndex);

    Kind kind() const { return kind_; }
    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const std::vector<core::ChapterEntry>& items() const { return items_; }
    bool loading() const { return loading_; }
    bool ended() const { return ended_; }  // no more rows will come (window, server's end, cap)
    bool moreFailed() const { return moreFailed_; }  // a later page failed: the list keeps what it has; reload() tries again
    int pages() const { return pages_; }    // pages loaded for this list
    int hidden() const { return hidden_; }  // chapters the server sent that the rule hid
    // Updates: the newest fetchedAt the server sent (shown or not; 0 before the first page).
    std::int64_t newestFetchedAt() const { return newest_; }

    // listRevision(): the list was replaced (start, reload): the view reloads it. itemsRevision(): rows changed in place
    // (an action): redraw. Neither changes when rows are only added at the end.
    std::uint64_t listRevision() const { return listRevision_; }
    std::uint64_t itemsRevision() const { return itemsRevision_; }

    // X / the row menu: mark items()[index] read, or unread (unread also goes back to the first page, D43). One
    // updateChapters; the row shows what the server saved. False (no request) for a wrong index, nothing to change, or
    // while another action runs.
    bool setRead(std::size_t index, bool read);
    bool toggleRead(std::size_t index);  // read -> unread (also for a chapter with progress), else read
    bool busy() const { return acting_; }
    // An action or a reload failed since the last call (empty afterwards): the view tells the user.
    std::optional<api::ApiError> takeError();

    // Remembers the focused row across tab changes.
    void rememberFocus(std::size_t index) { focus_ = index; }
    std::size_t rememberedFocus() const { return focus_; }

  private:
    void loadPage();
    void onPage(int page, bool fresh, const api::ApiResult<core::NodeList<core::ChapterEntry>>& result);
    void maybeLoadMore();
    void changed();

    api::SuwayomiClient& client_;
    Kind kind_;
    Limits limits_;
    std::function<void()> listener_;
    std::function<void(const core::Manga&)> mangaChanged_;
    net::CancelToken cancel_;        // the page under way
    net::CancelToken actionCancel_;  // the action under way (and its manga reload)
    bool started_ = false;
    bool loading_ = false;
    bool acting_ = false;
    bool fresh_ = false;     // the page under way starts a new list (reload): replace the rows when it arrives
    State state_ = State::Loading;
    api::ApiError error_;
    std::optional<api::ApiError> pendingError_;
    std::vector<core::ChapterEntry> items_;
    core::UpdatesFilter filter_;  // Updates
    std::set<int> seenManga_;     // History
    int nextPage_ = 0;
    int pages_ = 0;
    int hidden_ = 0;
    int runPages_ = 0;            // pages loaded in this fill run
    std::size_t wantRows_ = 0;    // the run loads until there are this many rows
    std::int64_t newest_ = 0;
    bool ended_ = false;
    bool moreFailed_ = false;
    std::size_t focus_ = 0;
    std::uint64_t listRevision_ = 0;
    std::uint64_t itemsRevision_ = 0;
};

}  // namespace snx::ui
