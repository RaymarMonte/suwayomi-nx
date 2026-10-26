#include "core/graphql/Documents.hpp"

#include <iterator>

namespace snx::core::gql {

namespace {
#include "GraphqlDocuments.inc"  // generated at build time: constexpr kAllDocuments[]
}  // namespace

const std::vector<GqlDocument>& documents() {
    static const std::vector<GqlDocument> kDocuments(std::begin(kAllDocuments), std::end(kAllDocuments));
    return kDocuments;
}

GqlDocument document(std::string_view name) {
    for (const auto& doc : kAllDocuments) {
        if (doc.name == name) {
            return doc;
        }
    }
    return {};
}

}  // namespace snx::core::gql
