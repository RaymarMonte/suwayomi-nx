#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/models/Manga.hpp"
#include "core/models/Source.hpp"
#include "net/CancelToken.hpp"

// Search all (WP-14b, D56): one text searched in every shown source, a row per source. Without borealis, so it is
// unit-tested (test_ui_browse.cpp). GlobalSearchActivity shows it.
//
// The sources: the Browse list's order without the Last used row and without hidden languages, Local included, at most
// `sourcesMax` (core::searchSources). `concurrency` of them search at once (RuntimeProfile::globalSearchConcurrency); the
// others wait their turn in the list's order. Each asks the source's first page (SEARCH, page 1, the text, no filters) and
// keeps its first `covers` manga (each manga once). Rows never change places, so nothing jumps while the user moves.
// A new text starts over; the model's end (the screen closing) cancels what runs, and waiting sources never start.
namespace snx::ui {

class GlobalSearchModel {
  public:
    enum class RowState {
        Waiting,       // its turn hasn't come
        Searching,     // asked, no answer yet
        Found,         // manga has its first covers; found() how many on the first page
        NothingFound,  // the source answered with none
        Failed,        // error says why (the A on the card opens the source's screen, which has Retry)
    };

    struct Row {
        core::Source source;
        RowState state = RowState::Waiting;
        std::vector<core::Manga> manga;  // at most `covers`, in the source's order
        std::size_t found = 0;           // manga on the first page (each once)
        bool more = false;               // the source has more pages: "25+ found"
        api::ApiError error;             // when Failed
    };

    struct Limits {
        std::size_t concurrency = 3;  // RuntimeProfile::globalSearchConcurrency (at least 1)
        std::size_t covers = 5;       // RuntimeProfile::globalSearchCovers
        std::size_t sourcesMax = 100; // RuntimeProfile::globalSearchSourcesMax
    };

    // `sources`: every source the server has (SourcesModel::sources()); `hiddenLanguages`: the Languages checklist's.
    GlobalSearchModel(api::SuwayomiClient& client, const std::vector<core::Source>& sources,
                      const std::vector<std::string>& hiddenLanguages, Limits limits);
    ~GlobalSearchModel();  // cancels what runs; the waiting sources never start

    GlobalSearchModel(const GlobalSearchModel&) = delete;
    GlobalSearchModel& operator=(const GlobalSearchModel&) = delete;

    void setListener(std::function<void()> listener);  // after every change (through the client's deliver)

    // Searches every row for `query` (trimmed and cut by the caller), from the start: what ran for an earlier text is
    // cancelled. False (nothing happens) for an empty text.
    bool search(std::string query);

    // A manga joined or left the library (the "In library" tag on its cover). Nothing happens for one that isn't shown.
    void setInLibrary(int mangaId, bool inLibrary);

    const std::string& query() const { return query_; }
    const std::vector<Row>& rows() const { return rows_; }
    std::size_t doneCount() const;  // rows that answered (Found, Nothing found or Failed)
    std::size_t runningCount() const;
    bool finished() const { return !rows_.empty() && doneCount() == rows_.size(); }
    const Limits& limits() const { return limits_; }

    // listRevision(): a new search (the grid reloads). itemsRevision(): rows changed in place (redraw them).
    std::uint64_t listRevision() const { return listRevision_; }
    std::uint64_t itemsRevision() const { return itemsRevision_; }

  private:
    void startNext();  // fills the free places from the waiting rows, in order
    void ask(std::size_t row);
    void answered(std::size_t row, const api::ApiResult<core::BrowsePage>& result);
    void changed();

    api::SuwayomiClient& client_;
    Limits limits_;
    std::vector<Row> rows_;
    std::function<void()> listener_;
    net::CancelToken cancel_;
    std::string query_;
    std::uint64_t listRevision_ = 0;
    std::uint64_t itemsRevision_ = 0;
};

std::string_view globalSearchStateName(GlobalSearchModel::RowState state);  // "waiting", "searching", ... (test events)

}  // namespace snx::ui
