# Suwayomi-Server API notes

Researched 2026-10-21 against `Suwayomi/Suwayomi-Server` master and wiki. Stable: **v2.4.2366** (2026-09-30).
✅ = read in the server source. ❔ = expected, confirm from the schema dump in WP-02 (`tests/fixtures/schema.graphql`).

## Transport
- GraphQL endpoint: `POST {base}/api/graphql`, body `{"query": "...", "variables": {...}}`. ❔ (standard; WebUI uses it)
- Base URL may include a sub-path if the server sits behind a reverse proxy. Default port **4567**.
- The server sometimes returns relative URLs (pages, thumbnails). The client joins them with the base URL.

## Auth (`server.authMode`) ✅ wiki + source
| Mode | What the client does |
|---|---|
| `none` | nothing |
| `basic_auth` | `Authorization: Basic base64(user:pass)` on every request, images included |
| `simple_login` | cookie-based login page, which expires on restart / after 30 min. Low priority for us |
| `ui_login` | `login(input:{username,password})` → `{accessToken, refreshToken}`. Send `Authorization: Bearer <access>`. Also accepted: cookie `suwayomi-server-token` or `?token=` query param. Access token lives 5 min by default, refresh 60 d. `refreshToken(input:{refreshToken})` → `{accessToken}`. `login` throws if you're already logged in |

- Multi-user (preview, `ui_login` only): each user has their own library/progress. Permissions such as `DOWNLOAD_CHAPTERS`
  and `INSTALL_EXTENSIONS` gate some mutations, so the UI must handle "permission denied" nicely.
- Admin = user 1 = `server.authUsername/authPassword`.
- Plain HTTP sends credentials in clear text. Warn the user in the setup screen when the URL is `http://` and auth is on.

## Reading flow ✅ source (`ChapterMutation.kt`, `Page.kt`)
1. Chapters of a manga: query the manga's chapters ❔. To refresh from the source use `fetchMangaAndChapters` ❔
   (`fetchChapters` is deprecated "in Tachiyomix 1.6").
2. Start a chapter: `fetchChapterPages(input:{chapterId, format?})` → `{pages: [String], chapter, syncConflict}`.
   - `pages[i]` = `/api/v1/manga/{mangaId}/chapter/{chapterIndex}/page/{i}` (+ `?format=…` if given).
   - `format` is a MIME type, e.g. `image/jpeg`. The server converts with Java ImageIO before serving. Per-user
     `serveConversions` can also force conversion.
   - `syncConflict` may be set when KOReader-sync has newer remote progress (`deviceName`, `remotePage`). Show a prompt or ignore it at first.
3. Progress: `updateChapter(input:{id, patch:{lastPageRead, isRead, isBookmarked}})`. The server clamps `lastPageRead` to `[0, pageCount]`
   and stamps `lastReadAt`. For bulk use `updateChapters(input:{ids, patch})`.
4. Downloaded chapters are served from disk, others are fetched from the source on demand. The first load can be slow,
   so use generous timeouts for page requests (≥ 30 s) and show progress.

## Library and browsing ❔ (confirm names in WP-02)
- Server info/version: `aboutServer { name version buildType revision }`
- Categories: `categories { nodes { id name order } }`
- Library: `mangas(condition:{inLibrary:true}, …)` or per category `category(id){ mangas{…} }`. Paginated (`first/offset` or cursors).
- Manga: `manga(id)` with `thumbnailUrl`, `title`, `author`, `description`, `status`, `unreadCount`, `chapters`
- Sources/search: `sources`, `fetchSourceManga(input:{source, type: POPULAR|LATEST|SEARCH, page, query})`
- Library membership: `updateManga(input:{id, patch:{inLibrary}})`, category assignment mutation
- Extensions: `extensions`, `fetchExtensions`, `updateExtension(input:{id, patch:{install|update|uninstall}})`
- Downloads: `enqueueChapterDownloads`, `dequeueChapterDownloads`, `downloadStatus`. The WebUI uses subscriptions over WebSocket,
  so we'll poll instead to keep things simple.
- Library update: `updateLibrary` / `updateStatus`

The best reference for real query shapes is **Suwayomi-WebUI**'s GraphQL documents (TypeScript). Copy the *shape*, not the code.

## Useful for tests ✅
- Docker: `ghcr.io/suwayomi/suwayomi-server:stable|preview|<version>`, data at `/home/suwayomi/.local/share/Tachidesk`.
  Env: `AUTH_MODE`, `AUTH_USERNAME`, `AUTH_PASSWORD`, `JWT_TOKEN_EXPIRY` (set to e.g. `PT20S` to test refresh), `BIND_PORT`,
  `WEB_UI_ENABLED`, `KCEF_ENABLED=false` (faster start), `DEBUG`.
- Jar: `java -Dsuwayomi.tachidesk.config.server.rootDir=<dir> -Dsuwayomi.tachidesk.config.server.port=4568 … -jar Suwayomi-Server-*.jar`
  (any `server.*` key can be overridden with `-Dsuwayomi.tachidesk.config.<key>=<value>`). Also set `server.systemTrayEnabled=false`
  and `server.initialOpenInBrowserEnabled=false`.
- **Local Source**: put `<rootDir>/local/<Manga>/<Chapter>/<pages>` or `<Manga>/<chapter>.cbz` plus optional `cover.jpg` and `details.json`.
  It works with no extensions and no internet, which is ideal for deterministic tests.
- OPDS exists at `/api/opds/v1.2`. We don't use it.
