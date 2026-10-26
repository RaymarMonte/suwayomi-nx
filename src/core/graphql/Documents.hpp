#pragma once

#include <string_view>
#include <vector>

#include "core/graphql/Gql.hpp"

// The GraphQL documents, embedded from src/core/graphql/*.graphql at build time (EmbedGraphql.cmake). The contract test
// (tests/contract/) validates the same files against tests/fixtures/schema.graphql. Most code uses Operations.hpp instead.
namespace snx::core::gql {

// Every embedded document, sorted by name.
const std::vector<GqlDocument>& documents();

// One by name ("AboutServer"). An unknown name gives an empty document (name and text empty); tests check every name used.
GqlDocument document(std::string_view name);

}  // namespace snx::core::gql
