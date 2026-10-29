#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/AppConfig.hpp"
#include "core/models/Category.hpp"
#include "core/models/Manga.hpp"
#include "net/CancelToken.hpp"

// The library screen's state (WP-09): the category tabs and, per category, the manga loaded so far, sorted. Without borealis,
// so it is unit-tested (FakeHttpClient). LibraryTab (the view) shows it; MainActivity owns it, so it survives the tab frame
// deleting the tab's view, and the grid comes back at the same category and cell.
//
// Loading: the categories, then the category on screen one page at a time (RuntimeProfile::libraryPageSize). Sorted by
// title (the server's order) the next page loads when the grid shows the last half page. The other sorts need the whole
// category first, so it loads page after page and then sorts. A category's manga stay loaded while the user looks at
// another one; refresh() (X) forgets everything. The Default category is a tab only when it holds manga or is the only one.
namespace snx::ui {

// The grid's order. Title keeps the server's order (title, then id); the others fall back to it for ties.
void sortLibrary(std::vector<core::Manga>& manga, core::LibrarySort sort);

class LibraryModel {
  public:
    enum class State {
        Loading,  // the categories or the category's first page (all of it for Unread / LastRead) are on their way
        Ready,    // items() has manga; more may follow (loadingMore())
        Empty,    // the category has no manga (libraryEmpty(): the whole library has none)
        Failed,   // nothing to show: error() says why, the screen offers Retry
    };

    // `start`: the category and sort the user left the screen with. pageSize: RuntimeProfile::libraryPageSize (the client
    // asks for that many per page; this model uses it for when to ask for the next one).
    LibraryModel(api::SuwayomiClient& client, core::LibrarySettings start, int pageSize);
    ~LibraryModel();  // cancels what is under way

    LibraryModel(const LibraryModel&) = delete;
    LibraryModel& operator=(const LibraryModel&) = delete;

    // Called after every change of what the screen shows (through the client's deliver: the main thread in the app).
    // Empty while no view is open.
    void setListener(std::function<void()> listener);

    void start();    // loads the categories, then the start category. Only once; later calls do nothing.
    void refresh();  // forgets everything and loads it again (X, Retry); keeps the category if it still exists
    void selectCategory(std::size_t index);  // a tab
    void stepCategory(int step);             // L (-1) / R (+1); stops at the first and last tab
    void setSort(core::LibrarySort sort);
    // The grid shows cells up to `lastIndex` now: near the end of what is loaded, the next page is asked for.
    void showing(std::size_t lastIndex);

    State state() const { return state_; }
    const std::vector<core::Category>& categories() const { return categories_; }  // empty until they are known
    std::size_t categoryIndex() const { return current_; }
    const core::Category* category() const;  // the one on screen, nullptr while categories() is empty
    core::LibrarySort sort() const { return settings_.sort; }
    // What config.json keeps: the category on screen (once known) and the sort.
    const core::LibrarySettings& settings() const { return settings_; }

    const std::vector<core::Manga>& items() const { return items_; }  // the category on screen, sorted
    int totalCount() const;    // the category's manga on the server
    std::size_t loadedCount() const;  // loaded so far (shows progress while a whole category loads for a sort)
    bool loadingMore() const { return loading_ && state_ == State::Ready; }
    bool moreFailed() const;   // a later page failed: the grid keeps what it has; refresh() tries again
    bool libraryEmpty() const;  // Empty, and the only category is Default: there are no manga in the library at all
    const api::ApiError& error() const { return error_; }

    // The cell that had the focus in the category on screen, so the grid can come back to it.
    void rememberFocus(std::size_t index);
    std::size_t rememberedFocus() const;

  private:
    struct Shelf {  // one category's manga
        std::vector<core::Manga> loaded;  // the server's order
        std::set<int> ids;                // so a manga that moved between two page requests isn't shown twice
        int nextPage = 0;
        int total = 0;
        bool started = false;   // the first page arrived
        bool complete = false;  // every page arrived
        bool failed = false;    // a later page failed
        std::size_t focus = 0;
    };

    void loadCategories();
    void onCategories(const api::ApiResult<std::vector<core::Category>>& result);
    void loadPage(int categoryId);
    void onPage(int categoryId, const api::ApiResult<core::NodeList<core::Manga>>& result);
    void categoriesReady();
    void showCurrent();  // the shelf on screen -> items_ and state_, loading more if the sort needs it
    void fail(const api::ApiError& error);
    void cancelRunning();
    void changed();
    Shelf& shelf(int categoryId) { return shelves_[categoryId]; }
    const Shelf* currentShelf() const;

    api::SuwayomiClient& client_;
    core::LibrarySettings settings_;
    std::size_t nearEnd_;  // ask for the next page when the grid is this close to the end of what is loaded
    std::function<void()> listener_;
    net::CancelToken cancel_;  // the request under way
    bool started_ = false;
    bool loading_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    std::vector<core::Category> pending_;  // the categories while Default's first page decides whether it is a tab
    std::vector<core::Category> categories_;
    std::size_t current_ = 0;
    std::map<int, Shelf> shelves_;  // by category id
    std::vector<core::Manga> items_;
};

}  // namespace snx::ui
