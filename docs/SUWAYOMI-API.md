# Suwayomi-Server API notes

Researched 2026-10-21 against `Suwayomi/Suwayomi-Server` master and wiki. Stable: **v2.4.2366** (2026-09-30).
Checked in WP-02 (2026-10-25) against the real server v2.4.2366: its schema is `tests/fixtures/schema.graphql`, real answers are in
`tests/fixtures/responses/` (made by `tools/record-fixtures`, the query documents are in `tools/record-fixtures/queries/`).
The owner's own server runs the same version (v2.4.2366 Stable, checked 2026-10-26, D18).
✅ = seen in the schema or in a real answer from the test server (or read in the server source, where it says so).

## Transport ✅
- GraphQL endpoint: `POST {base}/api/graphql`, body `{"query": "...", "variables": {...}}`. GraphQL errors come back as HTTP 200 with
  `errors[]` (and `data` null or partial). An unknown id is an error too (`manga(id: 999999)` → "…declared as a non null type…").
- **Error messages carry a Java stack trace** after the first line (`"Exception while fetching data (/login) : Incorrect username or
  password.\r\n\r\njava.lang.Exception: …\tat …"`). Show only the text before the first `\r\n`, and strip the
  `Exception while fetching data (/<field>) : ` prefix.
- Base URL may include a sub-path if the server sits behind a reverse proxy. Default port **4567**.
- The server returns **relative URLs** for pages and thumbnails (`/api/v1/manga/1/thumbnail`). The client joins them with the base URL.
- Numbers that can be large are `LongString` (a string): ids of sources (`"0"` = Local source), times (`uploadDate` in ms,
  `buildTime` in s). `Cursor` is an opaque string (e.g. `"257-251"`).

## Auth (`server.authMode`) ✅ wiki + source + test server
| Mode | What the client does | Without / with wrong credentials |
|---|---|---|
| `none` | nothing | — |
| `basic_auth` | `Authorization: Basic base64(user:pass)` on every request, images included | HTTP **401**, body `Unauthorized` (plain text), images too |
| `simple_login` | cookie-based login page, which expires on restart / after 30 min. Low priority for us | not tested |
| `ui_login` | `login(input:{username,password})` → `{accessToken, refreshToken}`. Send `Authorization: Bearer <access>`. Also accepted: cookie `suwayomi-server-token` or `?token=` query param. `refreshToken(input:{refreshToken})` → `{accessToken}` | HTTP **200** with a GraphQL error `…: Unauthorized` (not 401). Expired token: the same. Wrong password: error `…: Incorrect username or password.` |

- Config values (server-reference.conf in the jar): `server.authMode = NONE | BASIC_AUTH | SIMPLE_LOGIN | UI_LOGIN`,
  `server.jwtTokenExpiry = "5m"` (access token, default 5 min), `server.jwtRefreshExpiry = "60d"`. The test server uses `"20s"`, and a token
  really stops working after 20 s; the refresh token then gives a new one that works (checked 2026-10-25).
- `login` throws if you're already logged in ("Cannot login while already logged-in"), so send it without the old token.
- Checked on the test server (WP-06, ui_login): `aboutServer` answers without a login. Images and thumbnails (`/api/v1/...`) without
  a valid token: HTTP **401**, body `Unauthorized` (text/plain). `refreshToken` works with no or a junk `Authorization` header. A bad
  refresh token is a GraphQL error (an access token: "Cannot use access token to refresh"; junk: a `JWTDecodeException` text).
  The tokens are HS256 JWTs with `iss`, `aud`, `token_type` (access|refresh) and `exp`, but **no `iat`**. There is no logout mutation.
- Multi-user (preview, `ui_login` only): each user has their own library/progress. Permissions such as `DOWNLOAD_CHAPTERS`
  and `INSTALL_EXTENSIONS` gate some mutations, so the UI must handle "permission denied" nicely. Library data made in `none` mode is
  the admin's (user 1) library in `ui_login` mode.
- Admin = user 1 = `server.authUsername/authPassword`.
- Plain HTTP sends credentials in clear text. Warn the user in the setup screen when the URL is `http://` and auth is on.

## Reading flow ✅
1. Chapters of a manga: `chapters(condition: {mangaId}, order: [{by: SOURCE_ORDER, byType: DESC}], first, offset)` →
   `nodes { id name chapterNumber sourceOrder uploadDate isRead isBookmarked lastPageRead pageCount isDownloaded }`, `totalCount`,
   `pageInfo`. (`manga(id){ chapters { nodes } }` works too.) To refresh from the source: `fetchMangaAndChapters(input:{id,
   fetchManga, fetchChapters})` → `{manga, chapters}`. `fetchManga` and `fetchChapters` are deprecated ("Tachiyomix 1.6").
   **`pageCount` is -1 until the chapter's pages were fetched once.**
2. Start a chapter: `fetchChapterPages(input:{chapterId, format?})` → `{pages: [String!]!, chapter, syncConflict}`.
   - `pages[i]` = `/api/v1/manga/{mangaId}/chapter/{sourceOrder}/page/{i}` (+ `?format=…` if given). Note: the chapter part is the
     chapter's `sourceOrder` (its index in the manga), not its id: Broken's chapter id 6 gives `/api/v1/manga/3/chapter/1/page/0`.
   - Broken images are still listed: a corrupt page, a 0-byte page and a 1×1 page each get a URL. The client finds out when decoding.
   - `format` is a MIME type, e.g. `image/jpeg`; the URLs then end in `?format=image%2Fjpeg`. The server converts with Java ImageIO before serving. Per-user
     `serveConversions` can also force conversion.
   - `syncConflict { deviceName remotePage }` may be set when KOReader-sync has newer remote progress. Show a prompt or ignore it at first.
3. Progress: `updateChapter(input:{id, patch:{lastPageRead, isRead, isBookmarked}})` → `{chapter}`; bulk: `updateChapters(input:{ids, patch})`.
   - **`lastReadAt`** (seconds) is stamped whenever a patch sends `lastPageRead`, even 0, and for every chapter in a bulk
     update; `isRead` or `isBookmarked` alone leave it alone (checked on the test server, WP-10, D43).
     WP-15b (v2.4.2366): even the same page again stamps it; `fetchChapterPages` and `setChapterMeta` (the strip spot) don't.
     It comes as a string of digits. The waiting-save queue (D53) uses it to tell "still as we left it" from "changed
     elsewhere": it asks `chapter(id: $id) { id lastPageRead isRead lastReadAt meta { key value } }` (`ChapterState`)
     before sending. A chapter that is gone answers only `errors[]` (the non-null `chapter` field, no `data`).
   The server stamps `lastReadAt` and **caps `lastPageRead` to `[0, pageCount]`** (source: `ChapterMutation.kt`). So:
   - progress sent before `fetchChapterPages` ever ran for that chapter is saved as **0** (pageCount is -1);
   - the cap is `pageCount` itself (10 for a 10-page chapter), one past the last page index.
   - Fields of one request don't wait for each other: a `fetchChapterPages` and an `updateChapter` in the same document can race.
     Send them as separate requests.
   - **The webtoon spot (WP-13b, D49):** the strip also saves how far down the page it is, as chapter meta: key
     `snx_strip_spot`, value `"<page>:<share>"` (4 decimals, e.g. `"2:0.4375"`; other apps ignore it). One request carries
     both fields (`UpdateStripProgress`): `updateChapter(input:{id, patch})` and `setChapterMeta(input:{meta:{chapterId, key,
     value}})`; both answer in the same document (they don't depend on each other, so the race above doesn't matter), and a
     save counts only when both did. Setting it again overwrites it. The app reads it back from `fetchChapterPages`'s
     `chapter { meta { key value } }` when it opens a chapter; the chapter list and `chapter(id)` have the same field. `deleteChapterMeta(input:{chapterId, key})` removes
     it (tests). Checked on the test server (v2.4.2366, 2026-11-10).
4. Downloaded chapters are served from disk, others are fetched from the source on demand. The first load can be slow,
   so use generous timeouts for page requests (≥ 30 s) and show progress.

## Library and browsing ✅
- Server info: `aboutServer { name version buildType buildTime }` → `"Suwayomi-Server"`, `"v2.4.2366"`, `"Stable"`. Also `github`,
  `discord`, `platformInfo`. `revision` is deprecated: "the version includes the revision as the patch number".
- Categories: `categories(order: [{by: ORDER}]) { nodes { id name order default } }`. The **Default category has id 0** (name
  `Default`, `default: true`) and is always there.
- Library: `mangas(condition: {inLibrary: true}, order: [{by: TITLE}], first, offset)` → `nodes`, `totalCount`,
  `pageInfo { hasNextPage hasPreviousPage endCursor }`. Pagination works with `first/offset` and with cursors (`after/before`).
  Sort: `MangaOrderBy` = `ID | TITLE | IN_LIBRARY_AT | LAST_FETCHED_AT`, `byType` = `ASC | DESC | …NULLS_FIRST/LAST`.
- Per category: `category(id) { mangas { nodes totalCount } }`. **`category(id: 0)` lists the manga that are in no category**;
  `mangas(condition: {categoryIds: [0]})` returns none. But `category(id).mangas` takes no paging arguments, so the app uses
  `mangas(condition: {inLibrary: true}, filter: {categoryId: {equalTo: id}}, first, offset)`, and `filter: {categoryId: {isNull: true}}`
  for the Default category (both checked on the test server, WP-04; `filter: {categoryId: {equalTo: 0}}` returns none).
- Manga: `manga(id)` → `title`, `thumbnailUrl` (relative, `/api/v1/manga/{id}/thumbnail`), `author`, `artist`, `description`
  (nullable), `genre: [String!]!`, `status: MangaStatus` (`UNKNOWN ONGOING COMPLETED LICENSED PUBLISHING_FINISHED CANCELLED ON_HIATUS`),
  `unreadCount`, `downloadCount`, `inLibrary`, `categories`, `chapters`, `source { id name displayName }`, and helpers such as
  `firstUnreadChapter`, `lastReadChapter`, `latestUploadedChapter`.
- Sources/search: `sources`, `source(id: LongString!)`, `fetchSourceManga(input:{source, type: POPULAR|LATEST|SEARCH, page, query?,
  filters?})` → `{mangas, hasNextPage}`. Local source id is `"0"`.
- Library membership: `updateManga(input:{id, patch:{inLibrary}})`, `updateMangas(input:{ids, patch})` (the patch has only `inLibrary`).
  Categories: `createCategory(input:{name})`, `updateMangaCategories(input:{id, patch})` and `updateMangasCategories(input:{ids, patch})`
  with `patch: {addToCategories, removeFromCategories, clearCategories}`.
- Extensions: `extensions`, `extension(pkgName)`, `fetchExtensions`, `updateExtension(input:{id: <pkgName>, patch:{install|update|uninstall}})`,
  `updateExtensions`.
- Downloads: `enqueueChapterDownloads(input:{ids})`, `dequeueChapterDownloads(input:{ids})`, `downloadStatus { state queue }`, plus
  `startDownloader`, `stopDownloader`, `deleteDownloadedChapters`. The WebUI uses subscriptions over WebSocket, so we'll poll instead.
- Library update: `updateLibrary(input:{categories?})`, status `libraryUpdateStatus` (`updateStatus` is deprecated), `updateStop`.
  Details below.

## Updates, history and the library update ✅ (test server v2.4.2366, 2026-11-13, for WP-15)
- **Units:** `fetchedAt` (when the server first found a chapter), `lastReadAt` and the manga's `inLibraryAt` are seconds;
  `uploadDate` and `lastUpdateTimestamp` are milliseconds. A Local chapter's `uploadDate` is its folder's file time.
- **New chapters across the library:** `chapters(filter: {inLibrary: {equalTo: true}}, order: [{by: FETCHED_AT, byType:
  DESC}, {by: SOURCE_ORDER, byType: DESC}], first, offset)`, with `manga { id title thumbnailUrl inLibraryAt }` per node
  (50 nodes ~17 KB, ~40 ms). Every chapter counts, also the ones a manga came with when it was added (fetched just before
  or in the same second as `inLibraryAt`); Mihon-style "new only" = `fetchedAt > manga.inLibraryAt`, decided by the client
  (a filter can't compare two fields). An update keeps old chapters' `fetchedAt`; a new one gets the update's time.
- **History:** `chapters(filter: {and: [{lastReadAt: {greaterThan: "0"}}, {or: [{isRead: {equalTo: true}}, {lastPageRead:
  {greaterThan: 0}}]}]}, order: [{by: LAST_READ_AT, byType: DESC}, {by: SOURCE_ORDER, byType: DESC}])`. **Trap:** fields
  next to `or` without an `and` are OR'ed with it (that answer listed chapters with `lastReadAt` 0); plain fields side by
  side are AND'ed. A bulk "mark unread" (`lastPageRead: 0`) stamps `lastReadAt` on all its chapters, so the "read or with
  progress" part keeps them out.
- **`updateLibrary(input: {})` answers with the previous job's status** (finished-looking, or all zero before the first
  update): don't trust it. `lastUpdateTimestamp { timestamp }` changes when an update starts (also when every manga is
  skipped) and stays the same during it; `libraryUpdateStatus { jobsInfo { isRunning totalJobs finishedJobs
  skippedMangasCount skippedCategoriesCount } }` counts up while it runs and keeps the last job's numbers afterwards.
  Done = the timestamp differs from the one read before starting and `isRunning` is false, **but not right away**
  (WP-15, D52): for a moment after the start the timestamp has already moved while `jobsInfo` still shows the previous
  job (not running, all finished), so a poll that soon sees "done" with the old counts. The app also waits until it has
  seen `isRunning`, or the counts changed, or the same state held for one poll interval. `mangaUpdates { status manga
  { id title } }` lists only the manga that ran (`PENDING RUNNING COMPLETE FAILED SKIPPED`); skipped ones are only
  counted. 156 Local manga took ~0.6 s.
- **The server skips by its settings** (`settings { excludeEntryWithUnreadChapters excludeNotStarted excludeCompleted
  excludeUnreadChapters }`, all true by default, like Mihon): a manga with unread chapters, one never started, a completed
  one. A fresh library with nothing read: every manga skipped. Categories have `includeInUpdate` (`UNSET` by default).
- `fetchMangaAndChapters` needs both `fetchManga` and `fetchChapters` (required, though deprecated). After a chapter's
  folder is removed it drops that chapter (a test can undo a "new chapter").

## Search and categories ✅ (test server v2.4.2366, 2026-11-13, for WP-24, D54)
- **Search the library:** `LibraryByCategory` with `filter: {title: {includesInsensitive: $q}}` instead of the category
  filter, so no new document. It works across categories and on non-ASCII titles ("日本" finds the Japanese one). **Trap:**
  `%` and `_` are SQL wildcards ("%" matched all 157, "a_p" matched Alpha). A backslash escapes them: the app escapes `\`,
  `%` and `_` (`core::likeEscape`).
- `createCategory(input: {name})` appends it (order = last + 1). Only an exact duplicate is refused ("'name' must be
  unique", with a long Java stack trace in the message); **an empty name, one of spaces, or one differing only in case is
  accepted**. **`updateCategory(input: {id, patch: {name}})` checks nothing**: it accepts a taken name and renames Default
  (id 0). So the app checks names itself (`core::checkCategoryName`) and never offers Default.
- `updateCategoryOrder(input: {id, position})` answers every category in the new order. `position` is 1-based among the
  categories after Default (Default keeps order 0); 0 is refused, past the end moves it to the end, Default doesn't move.
- `deleteCategory(input: {categoryId})` → `category { id }`, `mangas { id }`. Its manga stay in the library (one in no
  other category is then in Default); the others' `order` is renumbered 1..n. Deleting id 0 or a gone id answers
  `category: null` and does nothing.
- `updateMangaCategories(input: {id, patch: {addToCategories, removeFromCategories, clearCategories}})` → `manga {
  categories { nodes { id name order } } }`. Adding 0 is ignored; an unknown id fails (a foreign-key error).
- A category's `mangas { totalCount }` counts its library manga (Default: the ones in no category).

The best reference for real query shapes is **Suwayomi-WebUI**'s GraphQL documents (TypeScript). Copy the *shape*, not the code.

## Useful for tests ✅
- The test server: `tools/test-server/` (README there, D29). Fixture manga: docs/TESTING.md.
- Jar: `java -Dsuwayomi.tachidesk.config.server.rootDir=<dir> -Dsuwayomi.tachidesk.config.server.port=4568 … -jar Suwayomi-Server-*.jar`.
  Any `server.*` key from the jar's `server-reference.conf` can be set with `-Dsuwayomi.tachidesk.config.<key>=<value>`. For a quiet
  test server: `systemTrayEnabled=false`, `initialOpenInBrowserEnabled=false`, `webUIEnabled=false`, `webUIUpdateCheckInterval=0`,
  `kcefEnabled=false`, `globalUpdateInterval=0`, `backupInterval=0`. It answers about 4.5 s after Java starts.
- Docker (for CI later, not tried yet): `ghcr.io/suwayomi/suwayomi-server:stable|preview|<version>`, data at
  `/home/suwayomi/.local/share/Tachidesk`. Env: `AUTH_MODE`, `AUTH_USERNAME`, `AUTH_PASSWORD`, `JWT_TOKEN_EXPIRY`, `BIND_PORT`,
  `WEB_UI_ENABLED`, `KCEF_ENABLED=false`, `DEBUG`.
- **Local Source** (source `LocalSource.kt`, checked on the test server): `<rootDir>/local/<Manga>/<Chapter>/<pages>` or
  `<Manga>/<chapter>.cbz`, plus optional `cover.jpg` and `details.json`. Works with no extensions and no internet.
  - The folder name is the title. `details.json` (title, author, artist, description, genre list, `status` as a number: 0 unknown,
    1 ongoing, 2 completed, 3 licensed) overrides it.
  - Chapter number = parsed from the folder name ("Chapter 12" → 12); chapters are sorted by it. **Chapter upload date = the chapter
    folder's modification time** (so fixtures need fixed file times).
  - Pages = image files in name order (natural, case-insensitive). A file counts as an image by its extension, so a 0-byte `.jpg` is a page.
  - **No `cover.jpg` → the server copies the first page of the first chapter in as `cover.jpg`**, and writes a `.noxml` file into folders
    without metadata. So the server changes the local folder, and a manga without any cover isn't possible.
- OPDS exists at `/api/opds/v1.2`. We don't use it.
