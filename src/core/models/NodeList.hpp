#pragma once

#include <string>
#include <vector>

namespace snx::core {

// One page of a GraphQL list ({nodes, totalCount, pageInfo}). Called NodeList like on the server, so it isn't confused with the
// pages of a chapter.
template <typename T>
struct NodeList {
    std::vector<T> nodes;
    int totalCount = 0;        // all matches on the server, not just this page
    bool hasNextPage = false;  // more after this page (ask again with offset + nodes.size())
    std::string endCursor;     // opaque, empty if the query didn't ask for it
    int skipped = 0;           // entries that couldn't be read (no id, not an object) and were left out
};

}  // namespace snx::core
