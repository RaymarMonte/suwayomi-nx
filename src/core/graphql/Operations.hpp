#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/Result.hpp"
#include "core/graphql/Gql.hpp"
#include "core/models/Auth.hpp"
#include "core/models/Category.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/Manga.hpp"
#include "core/models/NodeList.hpp"
#include "core/models/PageList.hpp"
#include "core/models/ReadProgress.hpp"
#include "core/models/ServerInfo.hpp"

// The MVP's server operations (WP-04): for each one a request builder (document + variables) and a reader that turns the
// answer into a model. The api layer (WP-06) sends request.body() and hands the parsed answer to the reader.
//
// Readers: if the answer has the expected data, it is returned even when errors[] has entries too (partial data: GraphQL
// nulls only the fields that failed, and the models are tolerant of that). If the data is missing or null, the result is the
// first error (ErrorKind::Unauthorized or Server), or a Parse error if the server sent no error either.
namespace snx::core::gql {

// --- requests ---
GqlRequest aboutServer();
GqlRequest categories();
// first = page size (RuntimeProfile::libraryPageSize), offset = how many came before. The Default category (id 0) works too.
GqlRequest libraryByCategory(int categoryId, int first, int offset);
GqlRequest manga(int mangaId);
// newestFirst = by sourceOrder, descending. first = page size (RuntimeProfile::chapterPageSize).
GqlRequest chapters(int mangaId, int first, int offset, bool newestFirst = true);
// format: a MIME type such as "image/jpeg" when the server should convert the pages (D6); empty = as stored.
GqlRequest fetchChapterPages(int chapterId, std::string_view format = {});
// Sends lastPageRead (never below 0), plus isRead: true when progress.isRead (false leaves the read flag alone).
GqlRequest updateChapterProgress(const ReadProgress& progress);
// The chapter list's actions (WP-10): the patch's fields that are set, for every id. No ids is still a valid request.
GqlRequest updateChapters(const std::vector<int>& ids, const ChapterPatch& patch);
// "Refresh from source": the manga's details and chapter list asked from the source again (WP-10).
GqlRequest fetchMangaAndChapters(int mangaId);
GqlRequest login(std::string_view username, std::string_view password);
GqlRequest refreshToken(std::string_view refreshToken);

// --- answers ---
Result<ServerInfo> readAboutServer(const GqlResponse& response);
Result<std::vector<Category>> readCategories(const GqlResponse& response);
Result<NodeList<Manga>> readLibraryByCategory(const GqlResponse& response);
Result<Manga> readManga(const GqlResponse& response);
Result<NodeList<Chapter>> readChapters(const GqlResponse& response);
Result<PageList> readFetchChapterPages(const GqlResponse& response);
Result<ReadProgress> readUpdateChapterProgress(const GqlResponse& response);
Result<std::vector<Chapter>> readUpdateChapters(const GqlResponse& response);  // the changed chapters
Result<Manga> readFetchMangaAndChapters(const GqlResponse& response);        // the refreshed manga
Result<LoginTokens> readLogin(const GqlResponse& response);
Result<std::string> readRefreshToken(const GqlResponse& response);  // the new access token

}  // namespace snx::core::gql
