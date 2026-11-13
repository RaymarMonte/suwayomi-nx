#include "core/LibraryTools.hpp"

#include <algorithm>

namespace snx::core {

namespace {

constexpr std::string_view kIdeographicSpace = "\xE3\x80\x80";  // U+3000

bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::string asciiLower(std::string_view text) {
    std::string lower(text);
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return lower;
}

}  // namespace

std::string likeEscape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 4);
    for (char c : text) {
        if (c == '\\' || c == '%' || c == '_') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

std::string trimText(std::string_view text) {
    bool changed = true;
    while (changed && !text.empty()) {
        changed = false;
        if (isAsciiSpace(text.front())) {
            text.remove_prefix(1);
            changed = true;
        } else if (text.substr(0, kIdeographicSpace.size()) == kIdeographicSpace) {
            text.remove_prefix(kIdeographicSpace.size());
            changed = true;
        }
        if (text.empty()) {
            break;
        }
        if (isAsciiSpace(text.back())) {
            text.remove_suffix(1);
            changed = true;
        } else if (text.size() >= kIdeographicSpace.size() &&
                   text.substr(text.size() - kIdeographicSpace.size()) == kIdeographicSpace) {
            text.remove_suffix(kIdeographicSpace.size());
            changed = true;
        }
    }
    return std::string(text);
}

std::size_t utf8Length(std::string_view text) {
    return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) { return !isContinuation(c); }));
}

std::string utf8Cut(std::string_view text, std::size_t maxChars) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!isContinuation(text[i])) {
            if (chars == maxChars) {
                return std::string(text.substr(0, i));
            }
            ++chars;
        }
    }
    return std::string(text);
}

NameProblem checkCategoryName(std::string_view name, const std::vector<Category>& existing, int exceptId,
                              std::size_t maxChars) {
    if (name.empty()) {
        return NameProblem::Empty;
    }
    if (utf8Length(name) > maxChars) {
        return NameProblem::TooLong;
    }
    const std::string wanted = asciiLower(name);
    for (const Category& category : existing) {
        if (category.id != exceptId && asciiLower(trimText(category.name)) == wanted) {
            return NameProblem::Taken;
        }
    }
    return NameProblem::None;
}

std::string_view nameProblemName(NameProblem problem) {
    switch (problem) {
        case NameProblem::None:
            return "none";
        case NameProblem::Empty:
            return "empty";
        case NameProblem::TooLong:
            return "too_long";
        case NameProblem::Taken:
            return "taken";
    }
    return "none";
}

CategoryChange categoryChange(const std::set<int>& before, const std::set<int>& after) {
    CategoryChange change;
    for (int id : after) {
        if (id != kDefaultCategoryId && before.count(id) == 0) {
            change.add.push_back(id);
        }
    }
    for (int id : before) {
        if (id != kDefaultCategoryId && after.count(id) == 0) {
            change.remove.push_back(id);
        }
    }
    return change;
}

int categoryPosition(const std::vector<Category>& categories, int id, int step) {
    if (id == kDefaultCategoryId || step == 0) {
        return 0;
    }
    std::vector<int> ids;  // the categories after Default, in order
    for (const Category& category : categories) {
        if (category.id != kDefaultCategoryId) {
            ids.push_back(category.id);
        }
    }
    const auto found = std::find(ids.begin(), ids.end(), id);
    if (found == ids.end()) {
        return 0;
    }
    const long target = static_cast<long>(found - ids.begin()) + step;
    if (target < 0 || target >= static_cast<long>(ids.size())) {
        return 0;
    }
    return static_cast<int>(target) + 1;
}

}  // namespace snx::core
