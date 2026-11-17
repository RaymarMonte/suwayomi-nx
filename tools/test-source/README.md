# The test source (WP-23, D55)

Our own Suwayomi extensions, written in Java, for tests that need a real online source: browse with every filter type,
several languages, manga outside the library, chapters the server can download, an extension store with an update.
Everything stays on the PC: the server reads the catalogs from a test site on `http://127.0.0.1:4569` (loopback only).

| Extension (package) | Sources | Content warning | After the seed |
|---|---|---|---|
| SNX Test (`snx.testsource`) | "SNX Test" en (60 manga), ja (8) | SAFE | installed, v1 (`1.5.1`); the store also has v2 (`1.5.2`) |
| SNX Extra (`snx.testsource.extra`) | "SNX Extra" fr (5) | SAFE | not installed |
| SNX Mature (`snx.testsource.mature`) | "SNX Mature" en (3) | NSFW | not installed |

## How it works
- A **plain jar is an extension** for Suwayomi-Server v2.4.2366: our compiled classes plus a text `AndroidManifest.xml`
  (from `AndroidManifest.template.xml`). No Android SDK, no APK.
- `src/snx/testsource/CatalogSource.java` is the one source class. Each call (popular, latest, search, details, chapters,
  pages) asks the test site for `site/catalog-<id>.json` with the call's details in the query string; jwebserver ignores the
  query string and sends the file; the parse step reads the details back and picks the manga. `Filters.java` builds the
  filter list and applies the filter and sort rules. `ControlInterceptor.java` + `Control.java` apply the switch file
  (`site/control.json`) to every request, covers and page images included.
- `Main.java` (SNX Test), `extra/Main.java`, `mature/Main.java`: which sources each extension has. Every jar holds all classes;
  the manifest names its own `.Main`.
- The catalogs, covers, pages, the store index and the build list come from `tools/gen-fixtures/test-source.ts` (the content)
  and `gen-test-source.ts` (the writer). Tests import `test-source.ts` to predict what the server answers.

## How it's built
`tools/test-server/test-server.sh reset` (and `start`, when needed) does it all:
1. Node writes `.test-server/web/` (store index, icons, `store/build.json`, catalogs, covers, pages), when the generator changed
   (`web-site.stamp`).
2. `test-server.ps1 build-source` (Windows: `javac` and `jar` come with the PC's Temurin JDK 21; WSL has no JDK) compiles
   `src/` with `javac --release 21` against the pinned server jar (on the class path only, never packed in) and packs one jar
   per extension and version into `web/store/`, when the Java code, the template, the server version or the site changed
   (`web-source.stamp`). About 2 s. A missing `javac` means the PC has only a Java runtime: install the JDK (SETUP.md).
3. `test-server.ps1 start` starts `jwebserver -b 127.0.0.1 -p 4569 -d .test-server/web` before Java; the seed adds the store and
   installs SNX Test.
Built jars are never committed (`.test-server/` is git-ignored).

## Adding things
- **A manga**: add a title to its catalog in `tools/gen-fixtures/test-source.ts` (`CATALOG_EN` ...). The rules in
  `buildCatalog` give it an author, status, genres, type, colour, rank and chapters; `overrides` change one manga. Keep the
  unit tests green (`npx vitest run tools/gen-fixtures/test-source.test.ts`): every filter option must match some manga but
  not all, and the "Comet" counts are fixed. Changing a catalog changes popular/latest orders that tests predict with
  `selectManga()`, never with hard-coded lists.
- **A filter**: add it in `Filters.java` (`create()`, `addToUrl()`, `select()`) **and** in `test-source.ts` (`FILTER`,
  `FILTER_TYPES`, `FILTER_NAMES`, `selectManga()`). Positions are what tests address, so add new filters at the end.
- **An extension**: a new `Main` class in its own package under `src/snx/testsource/`, an entry in `EXTENSIONS` (and
  `storeIndex()`) in `test-source.ts`, and its source ids in `TEST_SOURCE_IDS` (the unit test checks the formula). Never rename a
  source or change its language: the name and language make its id (MD5 of `"<name lower case>/<lang>/1"`).

## When the server version changes
`server.json`'s version is part of the jar stamp, so `reset` recompiles against the new jar. If `javac` fails, the source API
changed: `javap -cp <server jar> eu.kanade.tachiyomi.source.online.HttpSource` shows the methods. Then run the live check
in two auth modes (`tools/test-server/README.md`); it checks every call, a download, the update and the store.
