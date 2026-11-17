#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/models/Manga.hpp"
#include "core/models/Source.hpp"
#include "net/CancelToken.hpp"

// A source's screen (WP-14, D56): its manga, Popular, Latest or a Search (a text and / or changed filters), page after page.
// Without borealis, so it is unit-tested (test_ui_browse.cpp). SourceActivity shows it.
//
// Paging (D42 like the library): page 1 first, the next one when the grid shows the last half page and the source says
// there is more. A manga seen twice shows once; a page that brings no new manga ends the list (the Local source answers
// every page with all of its manga); at most maxItems (RuntimeProfile::browseMaxItems). A failed first page is Failed (the
// screen offers Retry); a failed later page keeps what is there (moreFailed(), Retry at the end).
namespace snx::ui {

class SourceBrowseModel {
  public:
    enum class Mode { Popular, Latest, Search };
    enum class State {
        Loading,  // the first page is on its way
        Ready,    // items() has manga; more may follow
        Empty,    // the source found nothing
        Failed,   // the first page failed: error() says why
    };

    SourceBrowseModel(api::SuwayomiClient& client, core::Source source, std::size_t maxItems);
    ~SourceBrowseModel();  // cancels what is under way

    SourceBrowseModel(const SourceBrowseModel&) = delete;
    SourceBrowseModel& operator=(const SourceBrowseModel&) = delete;

    void setListener(std::function<void()> listener);  // after every change (through the client's deliver)

    void start();    // Popular, page 1. Only once.
    void refresh();  // page 1 again in the same mode (X)
    void retry();    // after Failed or moreFailed(): the page that failed again

    // Popular or Latest (Latest only when the source has one). Ends a search.
    void setMode(Mode mode);
    // A search: the text (trimmed by the caller) and the filters' changes (core::filterChanges) with their count. Both
    // empty: Popular again (what the server would answer anyway).
    void search(std::string query, nlohmann::json filterChanges, int filtersChanged);
    void clearSearch();  // Popular again
    // The grid shows cells up to `lastIndex`: near the end of what is loaded, the next page is asked for.
    void showing(std::size_t lastIndex);

    // A manga joined or left the library (the "In library" tag). Nothing happens for a manga that isn't listed.
    void setInLibrary(int mangaId, bool inLibrary);

    const core::Source& source() const { return source_; }
    Mode mode() const { return mode_; }
    const std::string& query() const { return query_; }
    int filtersChanged() const { return filtersChanged_; }
    const nlohmann::json& filterChanges() const { return changes_; }
    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const std::vector<core::Manga>& items() const { return items_; }
    bool hasNext() const { return !complete_; }  // the list may go on
    bool loadingMore() const { return loading_ && state_ == State::Ready; }
    bool moreFailed() const { return moreFailed_; }
    int pagesLoaded() const { return nextPage_ - 1; }

    // listRevision(): the list was replaced (a new mode, search, refresh): reload the grid. itemsRevision(): cells changed
    // in place (the tag): redraw them. Neither changes when a page is only added at the end.
    std::uint64_t listRevision() const { return listRevision_; }
    std::uint64_t itemsRevision() const { return itemsRevision_; }

    void rememberFocus(std::size_t index) { focus_ = index; }
    std::size_t rememberedFocus() const { return items_.empty() ? 0 : std::min(focus_, items_.size() - 1); }

  private:
    void restart();  // page 1 of the current mode, the list emptied
    void loadPage();
    void onPage(const api::ApiResult<core::BrowsePage>& result);
    void changed();

    api::SuwayomiClient& client_;
    core::Source source_;
    std::size_t maxItems_;
    std::function<void()> listener_;
    net::CancelToken cancel_;
    bool started_ = false;
    Mode mode_ = Mode::Popular;
    std::string query_;
    nlohmann::json changes_ = nlohmann::json::array();
    int filtersChanged_ = 0;
    State state_ = State::Loading;
    api::ApiError error_;
    std::vector<core::Manga> items_;
    std::set<int> ids_;
    int nextPage_ = 1;
    std::size_t nearEnd_ = 1;  // half of the first page
    bool loading_ = false;
    bool complete_ = false;
    bool moreFailed_ = false;
    std::uint64_t listRevision_ = 0;
    std::uint64_t itemsRevision_ = 0;
    std::size_t focus_ = 0;
};

std::string_view browseModeName(SourceBrowseModel::Mode mode);  // "popular", "latest", "search" (test events)

}  // namespace snx::ui
