# WP-23 Test source: our own extension, an extension store and a test site on the PC

Model: **Opus** · Size: M (split point below) · Needs: WP-02 (done) · Owner: – (nothing to install: the PC's JDK builds and
serves everything; no product questions, every choice below is the planner's default, D55)

## Goal
Today the test server has only the Local source: browse works but has one Sort filter, there is no extension and no
extension store, and the server can't download Local chapters. After this WP, `test-server.sh reset` + `start` also bring up
**an extension store and a test site on the PC** (loopback only), and the server has **our own test extension** installed:
sources in two languages with popular / latest / search, paging, **every filter type**, manga that aren't in the library
(for previews), **chapters the server can download**, a switch file that makes calls fail or slow down, and store entries
for WP-16 (an update, a not-installed extension in another language, one with a content warning). Still offline once the
server jar is downloaded (D29). WP-14 (browse), WP-16 (extensions) and WP-17 (server downloads) test against it.
No app code changes.

## Checked on the test server (v2.4.2366, 2026-11-17, a throwaway spike; goes into SUWAYOMI-API.md)
- **A plain .jar is an extension** (server code `Extension.kt`, PR #2182): a zip with the compiled JVM classes and a **text**
  `AndroidManifest.xml` (`package`, `android:versionCode`, `android:versionName` = lib version + ".x", `<uses-feature
  android:name="tachiyomi.extension"/>`, `<meta-data android:name="tachiyomi.extension.class" android:value=".Main"/>`, and
  `tachiyomi.extension.nsfw` or `tachiyomix.contentWarning`). No dex, no APK, no Android SDK. The spike compiled one Java
  class with `javac --release 21 -cp <server jar>` in ~1 s and packed it with `jar`; both come with the PC's Temurin JDK 21
  (the one that runs the test server), and so does `jwebserver`. WSL has no JDK. The server accepts lib versions 1.3–1.6.
- **The source API** in the server jar: `HttpSource` has only `getBaseUrl()` abstract; a Java class overrides `getName`,
  `getLang`, `getSupportsLatest`, `getFilterList` and the classic `popularMangaRequest/Parse`, `latestUpdatesRequest/Parse`,
  `searchMangaRequest/Parse`, `mangaDetailsRequest` (public) `/Parse`, `chapterListRequest/Parse`, `pageListRequest/Parse`.
  Models: `SManga.Companion.create()`, `SChapter.Companion.create()`, `new Page(index, url, imageUrl, null)`, `new
  MangasPage(list, hasNext)`; filters are `Filter.Header/Text/Select/Separator/CheckBox/TriState/Group/Sort` subclasses (Java
  passes every constructor argument). The jar also has `org.json` (Android's), jsoup, okhttp, `androidx.preference`.
- **A store is one JSON file** on any `http://` address (`addExtensionStore(input: {indexUrl})` took
  `http://127.0.0.1:4569/index.json`): `{name, badgeLabel, signingKey, contact: {website, discord}, extensionList:
  {extensions: [{name, packageName, resources: {apkUrl, iconUrl, jarUrl}, extensionLib, versionCode, versionName,
  contentWarning: SAFE|MIXED|NSFW, sources: [{id, name, language}]}]}, extensionListUrl: null}`. With a `jarUrl` the
  server downloads the jar, not the APK. **A byte-order mark at the start makes the server read it as protobuf and fail**
  (PowerShell 5.1 writes one; Node doesn't).
- `fetchExtensions` lists it (`isInstalled: false`); `updateExtension(input: {id: pkgName, patch: {install: true}})` installs
  it; the source shows in `sources` (`displayName` "Name (EN)") with all 8 filter types, each with its default; popular,
  latest and search (`query`, `filters: [{position, textState / selectState / checkBoxState / triState / sortState /
  groupChange: {position, …}}]`) reach our code. A newer `versionCode` in the store gives `hasUpdate: true`, `patch: {update:
  true}` loads the new code; `uninstall` removes the source and its jar file (also on Windows, where loaded jars are locked).
- Details, chapters and pages work; covers, pages and the extension icon reach clients only through the server
  (`/api/v1/manga/<id>/thumbnail`, `/api/v1/manga/<id>/chapter/<n>/page/<i>`, `/api/v1/extension/icon/<pkg>`), so the Switch
  never talks to the test site. **`enqueueChapterDownloads` downloads our chapter** (`isDownloaded: true`, files in
  `root/downloads/mangas/<source display name>/<title>/<chapter>/001.png …` + `ComicInfo.xml`). An unknown chapter id makes
  `enqueueChapterDownloads` wait 30 s and fail, and `fetchChapterPages` fail with "Collection is empty".
- Browsing creates manga rows (ids from 158 in browse order) and fetching chapters creates chapter rows (ids from 462), so
  **ids after the seed's are never fixed**: tests look them up by url or title. A manga in the library keeps its details
  title; one not in the library takes the title of the last browse. `jwebserver` sends the right types (`image/webp`,
  `image/jpeg`, `application/json`).
- Source ids are `MD5("<name lower case>/<lang>/1")`, first 8 bytes, top bit cleared (Tachiyomi's `generateId`; the spike's
  "SNX Spike"/en gave 7230610560190231314, the formula agrees). So: SNX Test en **1105946461371740061**, ja
  **1294398940357362604**, SNX Extra fr **7671842670262530014**, SNX Mature en **6796814882383758416**.

## Design
- **The extension is Java** (`tools/test-source/src/snx/testsource/*.java`), compiled against the pinned server jar (on the
  class path, never packed in) with the Windows JDK, `--release 21`. One shared source class reads its catalog from the
  test site; small factory / main classes per extension. Each jar = classes + a manifest written from a template. Built
  into `.test-server/web/store/` by `test-server.ps1 -Action build-source` (Windows: javac lives there), which
  `test-server.sh reset` calls when the stamp (Java sources, manifest template, generator, server version) changed, like
  `fixtures.stamp`. A missing `javac` / `jwebserver` (a JRE, not a JDK) stops with a clear message. Built jars are never
  committed.
- **The store and the site** are files in `.test-server/web/`, served by `jwebserver -b 127.0.0.1 -p 4569 -d <web>`,
  which `test-server.ps1` starts and stops with Java (WMI like Java; `stop` finds it by the folder on its command line;
  `status` reports it). Loopback only: no firewall rule, nothing for the owner. Store index `http://127.0.0.1:4569/store/
  index.json`, site `http://127.0.0.1:4569/site/`. Node writes the index and the catalogs (no byte-order mark).
- **Extensions in the store** (one Java code base):

  | Package | Name | Sources | Warning | After the seed | Versions in `store/` |
  |---|---|---|---|---|---|
  | `snx.testsource` | SNX Test | "SNX Test" en (60 manga), "SNX Test" ja (8) | SAFE | installed, v1 | v1 (`1.5.1`, code 1), v2 (`1.5.2`, code 2) |
  | `snx.testextra` | SNX Extra | "SNX Extra" fr (5) | SAFE | not installed | v1 |
  | `snx.testmature` | SNX Mature | "SNX Mature" en (3) | NSFW | not installed | v1 |

  The index normally offers SNX Test v1; a helper switches it to v2 (an update) and back. SNX Mature's manga are the same
  plain synthetic pages as the rest: only the flag differs.
- **The catalog** (`site/catalog-<source>.json`, plus covers and pages) comes from `tools/gen-fixtures` (new part, same
  pixel drawing and colour code, D31, same bytes every run; `fixtures.ts`-style definitions that tests import). en: 60 manga
  with key, title, author, artist, status (ongoing / completed / on hiatus / unknown), genres (Action, Comedy, Drama,
  Fantasy, Romance, Sci-Fi, Long Strip), type (Manga / Manhwa / Webtoon), colour yes/no, description, a popularity rank, and
  chapters (number, name, scanlator, date, pages). Popular = by rank, latest = by newest chapter date, 25 per page (en: 25 +
  25 + 10). Special manga: a ~120-character title, one with no cover, one whose cover answers 404, one with no chapters, a
  webtoon (genre "Long Strip", 800 × 12000 strips, so D49 opens it as a strip), one with a page that answers 404, and the
  word "Comet" in 3 en titles and 1 fr title, none in ja (a search across sources finds two sources, not the third). ja: 8
  Japanese titles. Four manga have pages of their own (JPEG, WebP, PNG; new colour-code series numbers, after checking what
  `pagecode.ts` can hold); the rest share one 2-chapter set, which keeps the generator fast. Titles avoid the Local
  fixtures' words (Alpha, Beta, …, Shelf, Broken). All dates fixed.
- **Filters** (the same list in every source, positions fixed so tests can address them): 0 Header "Filters also work
  without a search text", 1 Text "Author" (contains, any case), 2 Select "Status" (Any, Ongoing, Completed, On hiatus),
  3 Separator, 4 CheckBox "In colour", 5 Group "Genres" of TriStates (include = must have, exclude = must not have), 6 Group
  "Type" of CheckBoxes (none checked = all, else any checked), 7 Sort "Sort by" (Popularity, Title, Latest chapter; default
  Popularity, descending). All of them AND together with the title text (contains, any case). Every option matches at
  least one manga and not all of them. Search with no text and filters = a filtered browse; popular and latest ignore
  filters (the server sends them only with SEARCH).
- **The switch file** `site/control.json` (no file = all normal), read on every call, around OkHttp's cache:
  `{"fail": [{"lang": "ja", "call": "search"}], "delayMs": 0, "extraChapters": ["en-03"]}`. `fail` entries name a
  language (`en`, `ja`, `fr`, `*`) and a call (`popular`, `latest`, `search`, `details`, `chapters`, `pages`, `image`,
  `*`); a failing call throws an `IOException` "SNX test failure: <call>", so the server answers an error. `delayMs`
  (≤ 10 000) waits before each answer (spinners, cancelling). `extraChapters` shows a manga's held-back chapter (a library
  update or a download of a new chapter). The server keeps covers and pages it served once: an image failure test uses a
  manga not opened before, or clears the server's image cache first (check `clearCachedImages`).
- **The seed** (`seed.ts`, after the library steps, every `start`, safe to run again, all three auth modes): add the store
  if it's missing, `fetchExtensions`, install SNX Test if it isn't installed. It doesn't browse, so manga 1–157 and
  chapters 1–461 stay as they are. `reset` also puts the store back to v1 and deletes `control.json`.
- **Helpers for tests** (`tests/e2e/lib/test-source.ts`, used by later scenarios and the live check):
  `TEST_SOURCE_IDS`, `setTestSourceControl(c)`, `clearTestSourceControl()`, `offerTestSourceUpdate()` /
  `withdrawTestSourceUpdate()` (index on v2 / back on v1; going back reinstalls v1: uninstall, then install),
  `resetTestSource()` (switch file gone, store as seeded, SNX Test v1 installed, SNX Extra and SNX Mature not),
  `findSourceManga(sourceId, key)` (a manga's server id by its url, browsing if needed).

## Read first
STATUS.md; this file; `tools/test-server/README.md`, `test-server.sh`, `test-server.ps1`, `seed.ts`, `epsilon.test.ts` (the
live-check pattern); `tools/gen-fixtures/fixtures.ts`, `render.ts`, `gen-fixtures.ts`; `tests/e2e/lib/suwayomi.ts` (helper
style), `tests/e2e/lib/pagecode.ts` (how many series codes fit); TESTING.md "Fixture manga"; SUWAYOMI-API.md (to add the
facts). No app code: `src/` doesn't change.

## Deliverables
- [x] `tools/test-source/`: the Java sources (shared catalog source with the paging, search, filters, switch file, extra
      chapters; the three extensions' main classes), the manifest template, `README.md` (how it's built, how to add a
      manga / filter / extension, what to do when the server version changes).
- [x] Generator: the catalogs, covers, pages and `store/index.json` (source ids from the formula above), deterministic.
- [x] `test-server.ps1`: `build-source` (javac + jar per extension and version), `jwebserver` started / stopped / reported
      with Java, port 4569 checked like 4568. `test-server.sh`: the stamp, `reset` rebuilds when needed and resets the
      store and the switch file, `status` / `env` show the store address (`SNX_STORE_URL`).
- [x] Seed: the store and SNX Test v1, every start, all three auth modes.
- [x] `tests/e2e/lib/test-source.ts`: the helpers above.
- [x] npm unit tests: the generator gives the same bytes twice; catalog rules (counts, unique keys, every special manga
      there, every filter option matches some but not all, "Comet" placement, no outside address in any catalog or index
      entry: all start with `http://127.0.0.1:4569/`); the index has the server's fields and no byte-order mark; the
      source-id function gives the spike's 7230610560190231314 for "SNX Spike"/en.
- [x] Live check `tools/test-server/test-source.test.ts` (`SNX_LIVE_SERVER=1`, like `epsilon.test.ts`), on a fresh reset:
      the store and the three extensions (SNX Test v1 installed); en / ja sources and their filters; popular pages
      25 / 25 / 10 with `hasNextPage`; latest order; search by text; each filter type narrows to the set the catalog
      predicts; "Comet" across all sources; details, chapters, a page image through the server; a server download
      (`isDownloaded`); the switch file (an error, a delay, an extra chapter); update offered → `hasUpdate` → update → v2
      → back to v1; install and uninstall SNX Extra; SNX Mature's `contentWarning: NSFW`; remove and re-add the store. It
      leaves everything as the seed left it (`resetTestSource()`).
- [x] Docs: `tools/test-server/README.md` (store, site, port 4569, the switch file, helpers, bumping the server also
      rebuilds the source and runs the live check), TESTING.md (a "Test source" section next to the fixture manga; still
      offline), SUWAYOMI-API.md (the facts above), fix the "chapter 462" wording in TESTING.md and `suwayomi.ts` (ids after
      the seed aren't fixed), D55 (update if the build changes it), ROADMAP (row done), STATUS, CHANGELOG not needed (no
      app change).

## Acceptance
- `test-server.sh reset` + `start` in each auth mode end with the store listed and SNX Test v1 installed; `start` takes at
  most ~5 s longer than today (4.5 s); a rebuild after a source change takes ≤ 15 s.
- The live check green in `none` and `ui_login`; `npm test` + `npm run typecheck`; `tools/build.sh tests` (unchanged);
  `tools/build.sh integration` (3 modes) and all desktop e2e green with the extension installed (the app doesn't show
  sources yet, so nothing in it should change).
- No Switch runs: the app doesn't change (the console isn't needed for this WP).

## Split point
If it runs long: **SNX Extra, SNX Mature and the v2 / update helpers** move to WP-23b (needed before WP-16). WP-14 needs only
SNX Test v1 (en, ja), the catalogs, the filters and the switch file.

## Sonnet-safe chunks (Opus WPs)
The catalog's data tables (titles, authors, genre / status / type assignment that meets the rules above) and the npm tests
that check those rules, once Opus has written the generator's types; the README text. Opus writes the Java source, the
build and serve scripts, the seed and the live check, and reviews the rest.
