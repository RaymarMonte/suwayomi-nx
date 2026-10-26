// Fuzz target (SAFETY.md S6, WP-04): any bytes as a server answer. Reads them the way the app does (size and depth limits,
// GqlResponse), runs every answer reader, and tries every model's fromJson on every object and list inside the data.
// Nothing may crash, hang, leak or trip ASan/UBSan. Seeds: tests/fixtures/responses/ (real answers). Run: tools/build.sh fuzz
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/graphql/Gql.hpp"
#include "core/graphql/Operations.hpp"
#include "core/models/FromJson.hpp"

using namespace snx::core;

namespace {

void everyModel(const nlohmann::json& node) {
    (void)fromJson<ServerInfo>(node);
    (void)fromJson<Category>(node);
    (void)fromJson<Source>(node);
    (void)fromJson<Chapter>(node);
    (void)fromJson<Manga>(node);
    (void)fromJson<SyncConflict>(node);
    (void)fromJson<PageList>(node);
    (void)fromJson<ReadProgress>(node);
    (void)fromJson<LoginTokens>(node);
    (void)nodeListFromJson<Manga>(node);
    (void)nodeListFromJson<Chapter>(node);
    (void)nodeListFromJson<Category>(node);
}

// Recursion is bounded: the parser refuses anything nested deeper than JsonLimits::maxDepth (64). `budget` caps the work on
// inputs with very many small objects.
void walk(const nlohmann::json& node, int& budget) {
    if (--budget < 0) {
        return;
    }
    everyModel(node);
    if (node.is_structured()) {
        for (const auto& child : node) {
            walk(child, budget);
        }
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    Result<GqlResponse> parsed = parseGqlResponse(text);
    if (!parsed.ok()) {
        return 0;
    }
    const GqlResponse& response = parsed.value();
    (void)response.unauthorized();
    (void)response.firstError();
    (void)gql::readAboutServer(response);
    (void)gql::readCategories(response);
    (void)gql::readLibraryByCategory(response);
    (void)gql::readManga(response);
    (void)gql::readChapters(response);
    (void)gql::readFetchChapterPages(response);
    (void)gql::readUpdateChapterProgress(response);
    (void)gql::readLogin(response);
    (void)gql::readRefreshToken(response);
    int budget = 5000;
    walk(response.data, budget);
    return 0;
}
