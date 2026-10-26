#pragma once

#include <nlohmann/json.hpp>

#include "core/Json.hpp"
#include "core/Result.hpp"
#include "core/models/Auth.hpp"
#include "core/models/Category.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "core/models/NodeList.hpp"
#include "core/models/PageList.hpp"
#include "core/models/ReadProgress.hpp"
#include "core/models/ServerInfo.hpp"
#include "core/models/Source.hpp"

// JSON -> models (WP-04). The model headers themselves are plain structs without JSON, so screens can use them without this.
//
// Tolerant: a missing, null or wrong-typed field keeps the model's default, and unknown fields are ignored. A model fails
// (ErrorKind::Parse) only when it can't be used at all: the value isn't an object, or the id is missing or not a number (an
// item without an id can't be opened, updated or told apart). Never throws.
namespace snx::core {

// fromJson<Manga>(node) etc. `node` is the object itself (e.g. data.manga), not the whole answer.
template <typename T>
Result<T> fromJson(const nlohmann::json& node);

template <> Result<ServerInfo> fromJson<ServerInfo>(const nlohmann::json& node);
template <> Result<Category> fromJson<Category>(const nlohmann::json& node);
template <> Result<Source> fromJson<Source>(const nlohmann::json& node);
template <> Result<Chapter> fromJson<Chapter>(const nlohmann::json& node);
template <> Result<Manga> fromJson<Manga>(const nlohmann::json& node);
template <> Result<SyncConflict> fromJson<SyncConflict>(const nlohmann::json& node);
template <> Result<PageList> fromJson<PageList>(const nlohmann::json& node);  // needs "pages" (a list)
template <> Result<ReadProgress> fromJson<ReadProgress>(const nlohmann::json& node);
template <> Result<LoginTokens> fromJson<LoginTokens>(const nlohmann::json& node);  // needs a non-empty accessToken

// A GraphQL node list ({nodes, totalCount, pageInfo {hasNextPage endCursor}}). Entries that fail fromJson<T> are left out and
// counted in `skipped`, so one broken manga doesn't hide the whole library. Fails only when `list` has no "nodes" list.
template <typename T>
Result<NodeList<T>> nodeListFromJson(const nlohmann::json& list) {
    const nlohmann::json* nodes = json::array(list, "nodes");
    if (!nodes) {
        return Result<NodeList<T>>::fail(ErrorKind::Parse, "the list has no \"nodes\"");
    }
    NodeList<T> out;
    for (const auto& node : *nodes) {
        Result<T> item = fromJson<T>(node);
        if (item.ok()) {
            out.nodes.push_back(std::move(item).value());
        } else {
            ++out.skipped;
        }
    }
    out.totalCount = json::int32(list, "totalCount").value_or(static_cast<int>(nodes->size()));
    if (const nlohmann::json* pageInfo = json::object(list, "pageInfo")) {
        out.hasNextPage = json::boolean(*pageInfo, "hasNextPage").value_or(false);
        out.endCursor = json::string(*pageInfo, "endCursor").value_or("");
    }
    return out;
}

}  // namespace snx::core
