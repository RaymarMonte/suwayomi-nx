# Suwayomi-Server API notes

Researched 2026-10-21 against `Suwayomi/Suwayomi-Server` master and wiki. Stable: **v2.4.2366** (2026-09-30).
Checked in WP-02 (2026-10-25) against the real server v2.4.2366: its schema is `tests/fixtures/schema.graphql`, real answers are in
`tests/fixtures/responses/` (made by `tools/record-fixtures`, the query documents are in `tools/record-fixtures/queries/`).
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
   The server stamps `lastReadAt` and **caps `lastPageRead` to `[0, pageCount]`** (source: `ChapterMutation.kt`). So:
   - progress sent before `fetchChapterPages` ever ran for that chapter is saved as **0** (pageCount is -1);
   - the cap is `pageCount` itself (10 for a 10-page chapter), one past the last page index.
   - Fields of one request don't wait for each other: a `fetchChapterPages` and an `updateChapter` in the same document can race.
     Send them as separate requests.
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
