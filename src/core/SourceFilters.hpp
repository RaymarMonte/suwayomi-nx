#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A source's filters (WP-14, D56): the tree the server describes (source(id) { filters }), the values the user sets, and
// the change list fetchSourceManga takes (FilterChangeInput). Pure functions, unit-tested (tests/unit/test_core_browse.cpp).
//
// Positions: a filter is addressed by its place in the source's list, counting every filter (headers and separators too),
// and a group's filter by its place inside the group. So the list keeps every filter, also the kinds the app doesn't show.
// Only filters that differ from their default are sent; the server starts from the defaults on every call (SUWAYOMI-API.md).
namespace snx::core {

enum class FilterKind { Header, Separator, Text, Select, CheckBox, TriState, Sort, Group, Unknown };
std::string_view filterKindName(FilterKind kind);  // "header", "separator", "text", ... "unknown" (test events)

// A TriState filter: ignore (☐), include (✓: must have), exclude (✗: must not have). The server's names: IGNORE, INCLUDE,
// EXCLUDE.
enum class TriState { Ignore, Include, Exclude };
std::string_view triStateName(TriState state);
std::optional<TriState> triStateFromName(std::string_view name);
TriState nextTriState(TriState state);  // A on a TriState: ignore -> include -> exclude -> ignore

struct SortSelection {
    int index = 0;  // into SourceFilter::values
    bool ascending = false;
    bool operator==(const SortSelection& other) const { return index == other.index && ascending == other.ascending; }
};

// One filter's value. Only the field of the filter's kind counts.
struct FilterValue {
    std::string text;                   // Text
    int select = 0;                     // Select: index into values
    bool check = false;                 // CheckBox
    TriState tri = TriState::Ignore;    // TriState
    std::optional<SortSelection> sort;  // Sort (none: nothing chosen; a source may have no default)
};
bool sameValue(FilterKind kind, const FilterValue& a, const FilterValue& b);

struct SourceFilter {
    FilterKind kind = FilterKind::Unknown;
    std::string name;
    std::vector<std::string> values;  // Select and Sort: the choices
    FilterValue defaults;             // the source's default
    std::vector<SourceFilter> children;  // Group: its filters. A group inside a group isn't read (Unknown, no children).
};

// What the user set: one entry per filter of the list, in the same order. A group's own value is unused; its children's
// values are in `children`, one per child.
struct FilterSetting {
    FilterValue value;
    std::vector<FilterValue> children;
};
using FilterSettings = std::vector<FilterSetting>;

// S6 limits for what a source describes (RuntimeProfile: sourceFiltersMax, filterValuesMax, filterNameMaxBytes).
struct FilterLimits {
    std::size_t maxFilters = 300;   // filters with their children; the ones after the limit are left out (positions stay)
    std::size_t maxValues = 500;    // choices per Select / Sort; more are left out
    std::size_t maxNameBytes = 200; // names, choices and text defaults are cut there (never inside a UTF-8 character)
};

// The filters from the server's list (source.filters: [{__typename, name, <kind>Default, values, filters}], the defaults
// aliased by the SourceFilters document). An entry that isn't an object, or has an unknown __typename, becomes Unknown (it
// keeps its place). Never fails: no list gives no filters.
std::vector<SourceFilter> readFilterList(const nlohmann::json& list, const FilterLimits& limits);

// Every filter at its default (the start, and Reset).
FilterSettings defaultSettings(const std::vector<SourceFilter>& filters);
// `settings` made to fit `filters` (entries missing or extra after a reload of the list): missing ones take the default.
FilterSettings fitSettings(const std::vector<SourceFilter>& filters, FilterSettings settings);

// The filters that differ from their default, a group's children counted one by one ("Filters (2)").
int changedCount(const std::vector<SourceFilter>& filters, const FilterSettings& settings);
// fetchSourceManga's filters: [{position, textState | selectState | checkBoxState | triState | sortState}], a group's child
// as {position: <group>, groupChange: {position: <child>, <state>}}, one entry per changed child. Only what differs from
// the default; headers, separators and unknown kinds never. An empty array when nothing changed.
nlohmann::json filterChanges(const std::vector<SourceFilter>& filters, const FilterSettings& settings);

// At most maxBytes bytes of text, never cutting a UTF-8 character in half.
std::string cutBytes(std::string_view text, std::size_t maxBytes);

}  // namespace snx::core
