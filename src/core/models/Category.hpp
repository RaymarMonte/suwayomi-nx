#pragma once

#include <string>

namespace snx::core {

// The Default category always exists and has id 0. In the library it holds the manga that are in no other category.
inline constexpr int kDefaultCategoryId = 0;

struct Category {
    int id = 0;
    std::string name;
    int order = 0;           // the user's order (categories come sorted by it)
    bool isDefault = false;  // the server's "default" flag
};

}  // namespace snx::core
