#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/SourceFilters.hpp"
#include "net/CancelToken.hpp"

// A source's filters (WP-14, D56): loaded when the source's screen opens, the values applied to its search, and the copy the
// Filters screen edits. Without borealis, so it is unit-tested (test_ui_browse.cpp). The rules are in core/SourceFilters.
//
// The Filters screen shows rows: every filter except separators and unknown kinds, a group as a heading (closed at first)
// with its filters below it while open. Edits change the copy; apply() makes it the applied values (the source then
// searches), reset() puts the defaults into the copy, and leaving without apply() keeps the applied ones (beginEdit()
// starts the copy from them each time the screen opens). The values stay while the source's screen is open.
namespace snx::ui {

class SourceFiltersModel {
  public:
    enum class State {
        Loading,  // the filters are on their way
        Ready,    // filters() is the source's list (may be empty: no filters)
        Failed,   // error() says why, the Filters button offers Retry
    };

    // One row of the Filters screen.
    struct Row {
        std::size_t filter = 0;           // index into filters()
        std::optional<std::size_t> child;  // a group's filter: its index in the group
        int depth = 0;                     // 1 for a group's filters
    };

    SourceFiltersModel(api::SuwayomiClient& client, std::string sourceId);
    ~SourceFiltersModel();  // cancels what is under way

    SourceFiltersModel(const SourceFiltersModel&) = delete;
    SourceFiltersModel& operator=(const SourceFiltersModel&) = delete;

    void setListener(std::function<void()> listener);

    void start();  // Only once.
    void retry();  // after Failed

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const std::vector<core::SourceFilter>& filters() const { return filters_; }

    // What the search uses.
    int appliedCount() const { return core::changedCount(filters_, applied_); }
    nlohmann::json appliedChanges() const { return core::filterChanges(filters_, applied_); }

    // The Filters screen.
    void beginEdit();  // the copy = the applied values, every group closed
    const std::vector<Row>& rows() const { return rows_; }
    const core::SourceFilter* filterAt(std::size_t row) const;
    const core::FilterValue* valueAt(std::size_t row) const;  // the copy's value (nullptr for a group heading)
    bool isOpen(std::size_t filterIndex) const { return open_.count(filterIndex) > 0; }
    int editCount() const { return core::changedCount(filters_, edit_); }
    // Changed groups count their changed filters ("Genres (1 set)").
    int groupChangedCount(std::size_t filterIndex) const;

    // Each does nothing for a row of another kind or a wrong index.
    void toggleGroup(std::size_t row);
    void setText(std::size_t row, std::string text);
    void setSelect(std::size_t row, int index);
    void toggleCheck(std::size_t row);
    void cycleTri(std::size_t row);
    // The current sort again flips its direction; another one starts descending (like Mihon).
    void chooseSort(std::size_t row, int index);
    void reset();  // the copy back to the defaults
    void apply();  // the copy becomes the applied values

    std::uint64_t revision() const { return revision_; }  // rows() or a value changed

  private:
    void load();
    void rebuildRows();
    core::FilterValue* editValue(std::size_t row);
    void touched();

    api::SuwayomiClient& client_;
    std::string sourceId_;
    std::function<void()> listener_;
    net::CancelToken cancel_;
    bool started_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    std::vector<core::SourceFilter> filters_;
    core::FilterSettings applied_;
    core::FilterSettings edit_;
    std::set<std::size_t> open_;
    std::vector<Row> rows_;
    std::uint64_t revision_ = 0;
};

}  // namespace snx::ui
