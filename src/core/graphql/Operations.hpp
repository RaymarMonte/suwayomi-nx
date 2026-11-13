#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/Result.hpp"
#include "core/graphql/Gql.hpp"
#include "core/models/Auth.hpp"
#include "core/models/Category.hpp"
#include "core/models/Chapter.hpp"
#include "core/models/ChapterEntry.hpp"
#include "core/models/LibraryUpdate.hpp"
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
// WP-13b: the same plus the strip's spot (progress.spot, 0 when missing) as the chapter meta note, in one request.
GqlRequest updateStripProgress(const ReadProgress& progress);
// The chapter list's actions (WP-10): the patch's fields that are set, for every id. No ids is still a valid request.
GqlRequest updateChapters(const std::vector<int>& ids, const ChapterPatch& patch);
// "Refresh from source": the manga's details and chapter list asked from the source again (WP-10).
GqlRequest fetchMangaAndChapters(int mangaId);
GqlRequest login(std::string_view username, std::string_view password);
GqlRequest refreshToken(std::string_view refreshToken);
// WP-15, Updates and History. first = page size (RuntimeProfile::updatesPageSize / historyPageSize).
GqlRequest libraryUpdates(int first, int offset);  // the library's chapters, most recently found first
GqlRequest readingHistory(int first, int offset);  // chapters read or with progress, most recently read first
GqlRequest updateLibrary();                        // start a library update (the whole library)
GqlRequest stopLibraryUpdate();
// withManga: also the manga that ran and their status (the failed ones for the summary).
GqlRequest libraryUpdateStatus(bool withManga);
// WP-15b: one chapter's progress on the server now (the waiting-save queue's check before it sends).
GqlRequest chapterState(int chapterId);
// WP-24 (D54). The library search: LibraryByCategory with a title filter; `text` is sent escaped (core::likeEscape).
GqlRequest librarySearch(std::string_view text, int first, int offset);
GqlRequest manageCategories();  // every category with its manga count
GqlRequest createCategory(std::string_view name);
GqlRequest renameCategory(int categoryId, std::string_view name);
GqlRequest moveCategory(int categoryId, int position);  // position: core::categoryPosition (1-based, after Default)
GqlRequest deleteCategory(int categoryId);
// One manga's categories: add and remove in one request (no Default; empty lists are fine).
GqlRequest updateMangaCategories(int mangaId, const std::vector<int>& add, const std::vector<int>& remove);

// --- answers ---
Result<ServerInfo> readAboutServer(const GqlResponse& response);
Result<std::vector<Category>> readCategories(const GqlResponse& response);
Result<NodeList<Manga>> readLibraryByCategory(const GqlResponse& response);
Result<Manga> readManga(const GqlResponse& response);
Result<NodeList<Chapter>> readChapters(const GqlResponse& response);
Result<PageList> readFetchChapterPages(const GqlResponse& response);
Result<ReadProgress> readUpdateChapterProgress(const GqlResponse& response);
// Needs both answers (the page and the note): a note that wasn't saved fails the save, so it is sent again. The spot is
// read back from the note.
Result<ReadProgress> readUpdateStripProgress(const GqlResponse& response);
Result<std::vector<Chapter>> readUpdateChapters(const GqlResponse& response);  // the changed chapters
Result<Manga> readFetchMangaAndChapters(const GqlResponse& response);        // the refreshed manga
Result<LoginTokens> readLogin(const GqlResponse& response);
Result<std::string> readRefreshToken(const GqlResponse& response);  // the new access token
Result<NodeList<ChapterEntry>> readLibraryUpdates(const GqlResponse& response);
Result<NodeList<ChapterEntry>> readReadingHistory(const GqlResponse& response);
// Both only check that the server took it (its own status answer is the previous job's, never used).
Result<bool> readUpdateLibrary(const GqlResponse& response);
Result<bool> readStopLibraryUpdate(const GqlResponse& response);
Result<LibraryUpdateStatus> readLibraryUpdateStatus(const GqlResponse& response);
// The page, read flag, stamp and the strip's note. A chapter that is gone: the server's error (no data).
Result<ServerProgress> readChapterState(const GqlResponse& response);
// WP-24. The search answer has the shape of readLibraryByCategory's.
Result<NodeList<Manga>> readLibrarySearch(const GqlResponse& response);
Result<std::vector<Category>> readManageCategories(const GqlResponse& response);  // with mangaCount
Result<Category> readCreateCategory(const GqlResponse& response);                 // the new category
Result<Category> readRenameCategory(const GqlResponse& response);                 // the renamed category
Result<std::vector<Category>> readMoveCategory(const GqlResponse& response);      // every category, the new order
// The number of manga that were in it. A delete that did nothing (category: null) is a Server error.
Result<int> readDeleteCategory(const GqlResponse& response);
Result<std::vector<Category>> readUpdateMangaCategories(const GqlResponse& response);  // the manga's categories now

}  // namespace snx::core::gql
