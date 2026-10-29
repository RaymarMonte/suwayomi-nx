#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "net/CancelToken.hpp"

// The manga details screen's state (WP-10, D43): the manga's details and its whole chapter list, the list's sort and filter,
// where "Continue" starts, and the chapter actions (mark read / unread, bookmark, for one chapter or all before it).
// Without borealis, so it is unit-tested (FakeHttpClient). MangaActivity (the view) shows it.
//
// Loading: the details and the first page of chapters at the same time, then page after page until the whole list is
// there (RuntimeProfile::chapterPageSize per request, at most maxChapters). The list shows as soon as the first page is
// in; sorting, filtering, "Continue" and "all previous" work on the console. The actions wait for the server's answer and
// then show what it saved; one action at a time.
namespace snx::ui {

enum class ChapterSort { NewestFirst, OldestFirst };    // by the chapters' order in the manga (sourceOrder)
enum class ChapterFilter { All, Unread, Bookmarked };
std::string_view chapterSortName(ChapterSort sort);        // "newest", "oldest" (test events)
std::string_view chapterFilterName(ChapterFilter filter);  // "all", "unread", "bookmarked"

// Where "Continue" starts (D43).
struct ContinuePoint {
    enum class Kind {
        None,      // the manga has no chapters
        Start,     // nothing read yet: the first chapter, from its first page
        Continue,  // chapterId from page
        AllRead,   // every chapter is read
    };
    Kind kind = Kind::None;
    int chapterId = 0;
    int page = 0;  // page index
};

// The rule (D43), on the chapters in reading order (oldest first):
// 1. The chapter read most recently (biggest lastReadAt; a tie goes to the later chapter), among those that are read or
//    have progress: the server also stamps a chapter that is only marked unread (lastPageRead 0), which is no reading
//    position. Not finished: continue it at its lastPageRead.
// 2. Finished: the first unread chapter after it, at its lastPageRead; if every chapter after it is read, the first unread
//    one from the start (a gap). Without time stamps the last read chapter in reading order takes its place.
// 3. Nothing read at all: a chapter with progress (the first one), else Start at the first chapter. Every chapter read:
//    AllRead. `chapters` may come in any order.
ContinuePoint continueReading(const std::vector<core::Chapter>& chapters);

// "2026-10-01" (UTC) for a time in milliseconds since 1970; "" when unknown (0 or less, or past the year 9999).
std::string chapterDate(std::int64_t milliseconds);
// "12.5" for 12.5, "3" for 3.0; "" when unknown (below 0).
std::string chapterNumberText(double number);

class MangaModel {
  public:
    enum class State {
        Loading,  // the details or the first page of chapters are on their way
        Ready,    // details and chapters are here (items() may still be empty: the filter hides every chapter)
        Empty,    // the manga has no chapters
        Failed,   // nothing to show: error() says why, the screen offers Retry
    };

    // What a chapter's menu offers (actionsFor), and what act() does.
    enum class Action {
        MarkRead,
        MarkUnread,          // also back to the first page (lastPageRead 0, D43)
        Bookmark,
        RemoveBookmark,
        MarkPreviousRead,    // every chapter before it in reading order (lower sourceOrder), not the chapter itself
        MarkPreviousUnread,
    };

    // `manga`: what the library knows (title and cover show at once). maxChapters: RuntimeProfile::maxChapters (the client
    // asks for RuntimeProfile::chapterPageSize chapters per page).
    MangaModel(api::SuwayomiClient& client, core::Manga manga, std::size_t maxChapters);
    ~MangaModel();  // cancels what is under way

    MangaModel(const MangaModel&) = delete;
    MangaModel& operator=(const MangaModel&) = delete;

    // Called after every change of what the screen shows (through the client's deliver: the main thread in the app).
    void setListener(std::function<void()> listener);

    void start();  // the details and the chapters. Only once; later calls do nothing.
    void retry();  // after Failed: everything again
    // The chapter list again from the server (after reading). The list on screen stays until the new one is complete;
    // if that fails, it stays and takeActionError() says why. Does nothing while an action is under way.
    void reloadChapters();
    // Asks the source for the details and chapters again (fetchMangaAndChapters), then reloadChapters(). Does nothing while
    // an action is under way.
    void refreshFromSource();

    void setSort(ChapterSort sort);
    void setFilter(ChapterFilter filter);

    // The menu for chapter items()[index], always four entries in this order: MarkRead or MarkUnread, Bookmark or
    // RemoveBookmark, MarkPreviousRead, MarkPreviousUnread. Empty for a wrong index.
    std::vector<Action> actionsFor(std::size_t index) const;
    // Does `action` on chapter items()[index]. False (and no request) when nothing would change, when the index is wrong,
    // or while another action is under way.
    bool act(std::size_t index, Action action);
    // The read toggle (X): MarkUnread for a read chapter or one with progress, else MarkRead.
    Action toggleReadAction(std::size_t index) const;

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const core::Manga& manga() const { return manga_; }  // the library's copy until the details are in
    bool detailsLoaded() const { return detailsLoaded_; }
    const std::vector<core::Chapter>& items() const { return items_; }  // what the list shows: sorted and filtered
    const std::vector<core::Chapter>& chapters() const { return loaded_; }  // every chapter loaded, newest first
    int totalCount() const { return total_; }  // the manga's chapters on the server
    bool complete() const { return complete_; }  // every chapter is loaded (or maxChapters reached)
    bool moreFailed() const { return moreFailed_; }  // a later page failed: the list keeps what it has; retry() tries again
    bool busy() const { return busy_; }  // an action, reload or refresh is under way
    bool refreshing() const { return refreshing_; }  // refreshFromSource is under way
    ChapterSort sort() const { return sort_; }
    ChapterFilter filter() const { return filter_; }
    // From the chapters loaded so far; final once complete().
    ContinuePoint continuePoint() const { return continueReading(loaded_); }
    const core::Chapter* chapter(int chapterId) const;  // nullptr if not loaded

    // For the view: listRevision() goes up when the list's order or set of chapters changed (sort, filter, a reload, a
    // chapter that leaves the filter): reload the list. itemsRevision() goes up when chapters changed in place (an
    // action): redraw the rows. Neither changes when more chapters are only added at the end.
    std::uint64_t listRevision() const { return listRevision_; }
    std::uint64_t itemsRevision() const { return itemsRevision_; }
    // An action, reload or refresh failed since the last call (empty afterwards): the view tells the user.
    std::optional<api::ApiError> takeActionError();

    // The chapter that had the focus, so the list comes back to it after a reload.
    void rememberFocus(std::size_t index);
    std::size_t rememberedFocus() const;  // its index in items() now (or the nearest index, if it left the list)

  private:
    struct Loading {  // the chapter pages of one load
        std::vector<core::Chapter> chapters;
        std::set<int> ids;  // a chapter that moved between two page requests isn't listed twice
        int nextPage = 0;
        int total = 0;
        bool started = false;
    };

    void load();  // details + chapters (start, retry)
    void onDetails(const api::ApiResult<core::Manga>& result);
    void loadPage(bool replace);
    void onPage(bool replace, const api::ApiResult<core::NodeList<core::Chapter>>& result);
    void beginReload();
    void finishReload(bool ok, const api::ApiError& error);
    void applyChanged(const std::vector<core::Chapter>& changed);
    void rebuildItems(bool fieldsChanged);
    void updateState();
    void updateUnreadCount();
    std::vector<int> previousIds(const core::Chapter& target, bool wantRead) const;
    void fail(const api::ApiError& error);
    void changed();

    api::SuwayomiClient& client_;
    core::Manga manga_;
    std::size_t maxChapters_;
    std::function<void()> listener_;
    net::CancelToken loadCancel_;    // details and chapter pages
    net::CancelToken actionCancel_;  // actions and refreshFromSource
    bool started_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    bool detailsLoaded_ = false;

    Loading loading_;  // the load under way (the first one shows as it arrives; a reload replaces at the end)
    std::vector<core::Chapter> loaded_;  // newest first, as the server sends them
    int total_ = 0;
    bool firstPage_ = false;  // the first page of the first load is in
    bool complete_ = false;
    bool moreFailed_ = false;
    bool busy_ = false;
    bool refreshing_ = false;

    ChapterSort sort_ = ChapterSort::NewestFirst;
    ChapterFilter filter_ = ChapterFilter::All;
    std::vector<core::Chapter> items_;
    std::uint64_t listRevision_ = 0;
    std::uint64_t itemsRevision_ = 0;
    std::optional<api::ApiError> actionError_;
    int focusId_ = 0;
    std::size_t focusIndex_ = 0;
};

}  // namespace snx::ui
