#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/models/Category.hpp"

// The library tools' rules (WP-24, D54): the search text, category names, the picker's difference and where a category
// moves. Pure functions, unit-tested (tests/unit/test_core_library_tools.cpp).
namespace snx::core {

// The text for the server's title filter (includesInsensitive, an SQL LIKE inside): `\`, `%` and `_` get a backslash in
// front, so they match themselves (without it "%" matched every manga; checked on the test server, D54).
std::string likeEscape(std::string_view text);

// The text without spaces, tabs and line breaks at both ends (ASCII ones and the ideographic space U+3000, which the
// Japanese keyboard types).
std::string trimText(std::string_view text);

// Characters (UTF-8 code points) in text: every byte that isn't a continuation byte (10xxxxxx) counts.
std::size_t utf8Length(std::string_view text);
// At most maxChars characters of text, never cutting one in half.
std::string utf8Cut(std::string_view text, std::size_t maxChars);

// What is wrong with a category name the user typed (already trimmed).
enum class NameProblem {
    None,
    Empty,    // nothing left after trimming
    TooLong,  // more than maxChars characters
    Taken,    // another category (Default too) has it, ignoring case (ASCII letters)
};
// exceptId: the category being renamed (its own name doesn't count as taken); -1 for a new one. The server accepts empty
// names and renames to a taken name (D54), so the app checks this before asking it.
NameProblem checkCategoryName(std::string_view name, const std::vector<Category>& existing, int exceptId,
                              std::size_t maxChars);
std::string_view nameProblemName(NameProblem problem);  // "none", "empty", "too_long", "taken" (test events)

// The picker's difference: the categories to add and to remove (sorted), never Default (id 0, not a real category).
struct CategoryChange {
    std::vector<int> add;
    std::vector<int> remove;
    bool empty() const { return add.empty() && remove.empty(); }
};
CategoryChange categoryChange(const std::set<int>& before, const std::set<int>& after);

// Where category `id` goes when it moves `step` places (-1 up, +1 down) in `categories` (sorted by order): the server's
// position for updateCategoryOrder, 1-based among the categories after Default. 0 when it can't move (Default, the first
// one up, the last one down, an unknown id, step 0).
int categoryPosition(const std::vector<Category>& categories, int id, int step);

}  // namespace snx::core
