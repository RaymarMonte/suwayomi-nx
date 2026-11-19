# WP-14 Browse: sources, a source's manga, filters, search across sources, add to and remove from the library

Model: **Opus** · Size: **L** (split point below) · Needs: WP-24 (done), WP-23 (done) · Owner: – (answers given 2026-11-17;
optional: try it on your server)

## Goal
After this WP the Browse tab (a placeholder until now) lists the server's **sources by language**, opens a source's manga
(**popular / latest / search**, with the source's **filters**), **searches across all sources** at once, opens a **preview**
of a manga that isn't in the library (the details screen, loaded from the source), and **adds it to the library** with
WP-24's category picker, after a **same-title warning**. A manga also **leaves the library** from the Switch now. Everything
is tested against WP-23's test source. WP-16 later adds an Extensions part to the same tab.

## Owner's answers (2026-11-17)
1. **Adding: like Suwayomi's web UI.** When the user has categories (besides Default), the checklist opens with the categories
   marked "default" on the server already ticked; Add with none ticked = Default. The checklist has **"Don't ask again"**:
   from then on a manga goes straight into the "default"-marked categories (or Default); Settings turns asking back on. No
   categories: added at once. (Checked in their code on 2026-11-17: Mihon asks by default, nothing pre-ticked; the web UI's
   `showAddToLibraryCategorySelectDialog` is on by default, pre-ticks `Categories.getDefaults()`, and has "Don't show again".)
2. **Same-title warning: yes**, like both apps: before adding, a manga in the library with the same title (any source, any
   case) gives a box: "You have an entry in your library with the same name" with **Show entry**, **Add anyway**, **Cancel**.
3. **Remove from library: yes, with a confirm**, on the details screen's menu and on the Y cover menus (Library and Browse).
4. **Search across sources: a row per source**: a card with the source's name and count, then its first 5 covers; Left / Right
   moves along a row, A on a cover opens it, A on the card opens that source's full results.
5. **Languages: all shown, some can be hidden.** Every language that has a source shows (Local first); a **Languages**
   checklist on the Browse tab hides some (kept in config.json); the search across sources skips hidden languages too.
6. **18+ sources** (content warning NSFW) **show like any other, with a small "18+" tag.** No setting.

## Checked on the test server (v2.4.2366, 2026-11-17; goes into SUWAYOMI-API.md)
- **Sources:** `sources(order: [{by: NAME}]) { nodes { id name displayName lang iconUrl supportsLatest isConfigurable
  contentWarning } }` → Local (`id "0"`, **`lang "localsourcelang"`**, display name "Local source"), "SNX Test (EN)" `en`,
  "SNX Test (JA)" `ja`. `iconUrl` is relative (`/api/v1/extension/icon/<pkg>`, a PNG through the server; Local has one too).
- **Filters:** `source(id) { filters { __typename ... on GroupFilter { name filters { ... } } } }`. **Trap:** the union's
  `default` fields have different types (String, Boolean, Int, TriState, SortSelection), so one query must alias them
  (`textDefault: default`, `checkDefault: default`, …) or the server refuses it ("FieldsConflict"). SNX Test answers its 8
  filters with their defaults; Local has one `SortFilter` "Order by" (Title, Date; default Title ascending). `SortFilter.default`
  may be null (schema).
- **Browse:** `fetchSourceManga(input: {source, type: POPULAR | LATEST | SEARCH, page, query, filters})` is a **mutation**
  (never repeated) → `mangas { id title thumbnailUrl inLibrary initialized … } hasNextPage`. **Pages start at 1** (SNX Test en:
  25 + 25 + 10, `hasNextPage` false on page 3, page 4 empty; our test source also answers page 0 like page 1). **Local ignores
  `page`:** every page is all 157 manga with `hasNextPage: false`. Search with `query` "" or none and no filters = popular.
- **Filters reach the source only with SEARCH** (D55) and **don't stick:** the next call without filters is unfiltered.
  Every type works: `textState`, `selectState`, `checkBoxState`, `triState: INCLUDE | EXCLUDE`, `sortState: {index,
  ascending}`, `groupChange: {position, <state>}`; two changes in the same group = two entries with the same `position`.
  A wrong position ("Index 99 out of bounds") or the wrong state for a filter's type ("Expected select state change at
  position 2") is a GraphQL error.
- **A browsed manga is a bare row:** `initialized: false`, no author / genres / description, `chapters { totalCount }` 0.
  `fetchMangaAndChapters(input: {id, fetchManga: true, fetchChapters: true})` (our existing document) fills both (55 ms on
  the test source); afterwards `initialized: true`, 7 chapters.
- **Covers:** `thumbnailUrl` is null for a manga without a cover (en-05, nothing to load); a broken one (en-06) answers 500.
- **Errors and delays** (the switch file): a failing call answers `fetchSourceManga: null` with the error "Exception while
  fetching data (/fetchSourceManga) : SNX test failure: search" plus a Java stack trace; `delayMs: 4000` gave 4.07 s.
- **Adding:** `updateManga(input: {id, patch: {inLibrary: true}})` sets `inLibrary` and `inLibraryAt`, **and nothing else**:
  an unopened manga stays `initialized: false` with no chapters, and on this server **no category is applied**, not even one
  marked `default` (tried with "Long series" marked; newer servers apply the marked ones when the manga has no categories,
  per `Library.kt` on the server's master branch). `updateMangaCategories` on a manga **not** in the library sets its
  categories but doesn't add it. **Both in one document work** (top-level mutation fields run in order): `a: updateManga(…
  inLibrary: true)`, `b: updateMangaCategories(… {clearCategories: true, addToCategories: [ids]})` → exactly those
  categories. **Trap:** b's `manga { inLibrary }` once answered `false` right after a set it to true: read `inLibrary` from
  a, the categories from b.
- **Removing:** `updateManga(… {inLibrary: false})` **keeps the manga's categories** (also the server's code): re-added
  later without `clearCategories`, it would come back in its old ones. So an add always sends clear + the chosen set.
- **Categories' `default` flag** (`categories { nodes { default } }`; Default id 0 has it too and is never counted):
  `updateCategory(input: {id, patch: {default: true}})` works. It means "tick this for new manga" (the web UI's meaning).
- **Same title:** `mangas(condition: {inLibrary: true}, filter: {title: {likeInsensitive: $t}})` (what the web UI sends,
  unescaped) found "日本語タイトル テスト" and "ALPHA"; `"alph_"` matched Alpha too, `"alph\_"` didn't (WP-24's `likeEscape`
  applies); a leading space matters. There is no `equalToInsensitive`.

## Design
- **Where things live.** The Browse tab (`ui/browse/BrowseTab`) shows `SourcesModel`, owned by MainActivity like the library
  (D42: borealis deletes a tab's view on a tab change). A source's grid, its filters, the search across sources and the
  Languages checklist are Activities on top, each with its own model. config.json gets a `browse` section (no migration):
  `hiddenLanguages` (codes), `askCategories` (true), `lastSourceId` ("").
- **The Sources list** (Browse tab): header "Browse" with **Search all**, **Languages** (and room for WP-16's Extensions);
  a one-column `CoverGrid` of 80 px rows: the source's icon (through the image loader, disk cache), its display name, an
  "18+" tag for `contentWarning` NSFW (MIXED gets none). The **language label sits on the first row of each language**
  (WP-15's day-label pattern). Order: **Last used** (one row, when known and shown), then Local, then languages by name
  (core's table: "en" → English, "ja" → Japanese, "pt-BR" → Portuguese (Brazil), "all" → All languages, "other" → Other,
  unknown codes as they are), then sources by name. A / tap opens the source (and makes it Last used). X refresh, B back to
  the tab bar. Only Local: "Only the Local source is installed. Install extensions on the server (Browse → Extensions comes
  with WP-16)." Not shown: source settings (`isConfigurable`, later), pins.
- **Languages** (`LanguagesActivity`): a checklist of the languages that have sources (Local included), all ticked except the
  hidden ones; Done saves `hiddenLanguages`; at least one stays ticked.
- **A source's screen** (`SourceActivity` + `SourceBrowseModel`): the header has the source's name, **Popular | Latest** tabs
  (Latest only with `supportsLatest`; L / R or a tap), **Search** (askText, `searchMaxChars`) and **Filters** ("Filters (2)"
  with 2 changed). A search text and / or changed filters switch to SEARCH: the bar shows `Search: "comet"`, the filter count
  and **Clear** in place of the tabs (WP-24's search bar); B or Clear ends it (popular again), then B leaves. The grid is the
  library's cover grid (D42 sizes, `CoverGrid`, covers at Interactive priority, the row below at Prefetch), with an **"In
  library"** tag on covers already in the library (the library's cover cell moves to `components/CoverCell` with an optional
  tag). Paging: page 1 first, the next one when the grid shows the last half page and `hasNextPage`; a manga seen twice shows
  once (D42); **a page that brings no new manga ends the list** (Local ignores `page`); at most `browseMaxItems` (S6). A or a
  tap: the preview. Y: the cover's menu (Open, Add to library... / Remove from library...). X: refresh (page 1 again).
  MINUS: Popular, Latest, Search..., Filters..., Refresh. A failed first page: the error with Retry; a failed later page: the
  grid keeps what it has, "Couldn't load more" + Retry at the end (D42). The server's error text is cut to the message
  ("SNX test failure: search": the part after "Exception while fetching data (…) : ", first line only). Filters stay while the
  screen is open, not in config.json.
- **Filters** (`FiltersActivity` + `SourceFiltersModel`; the rules in core `SourceFilters`): the filters are loaded when the
  source screen opens (a failure: Filters says why and offers Retry). A one-column list of 72 px rows: Header (text, A does
  nothing), Text ("Author: any" / the text; A → askText, `filterTextMaxChars`), Select ("Status: Any"; A → a dropdown), CheckBox
  (☐ / ☑; A toggles), TriState (☐ / ✓ include / ✗ exclude; A cycles), Sort ("Sort by: Popularity ↓"; A → a dropdown, the
  current one again flips the direction), Group (a heading "Genres (1 set) ▸", A opens or closes it, its filters indented below,
  closed at first). Separators aren't shown (they still count for positions). **Apply** (X, button) hands the state back and the
  source searches; **Reset** (Y, button) puts the defaults back; B leaves without changes. Only filters that differ from their
  default are sent: `{position, <state>}`, a group's child as `{position: group, groupChange: {position: child, <state>}}`, one
  entry per changed child. A Group inside a Group isn't asked for (the query reads two levels); unknown filter types are
  skipped but keep their position. Limits (S6): `sourceFiltersMax` filters with children, `filterValuesMax` values per
  Select / Sort, names cut to 200 bytes.
- **Search all** (`GlobalSearchActivity` + `GlobalSearchModel`): askText first (from the Browse tab's Search all or its MINUS
  menu), then every shown source once (hidden languages left out, Local included, the list's order without the Last used
  row, at most `globalSearchSourcesMax`)
  gets a SEARCH with the text, `globalSearchConcurrency` at a time (Full 3); the rest wait. A `CoverGrid` with **exactly 6
  columns** (cell width from the screen width): per source one row, its **card** (icon, name, state: Waiting / Searching... /
  3 found / Nothing found / Failed: why) and up to `globalSearchCovers` (5) covers; empty places can't take the focus
  (`GridSource::focusable(index)`, new; a move into one lands on the row's last cell that can). Rows keep their order (nothing
  jumps while the user moves). A on the card: the source's screen searching that text (Nothing found and Failed rows too, so
  Retry is there); A on a cover: the preview; Y: the cover's menu. The header: `Search all: "comet"`, "2 of 3 sources done",
  **Search** (a new text). Leaving cancels what runs; waiting sources never start.
- **The preview** is the details screen (`MangaActivity`). Opening a manga that is not `initialized` or has no chapters
  (Mihon's rule; `initialized` added to the Manga document) first asks the source (`refreshManga`, "Loading from SNX Test
  (EN)...", the cover and title show meanwhile), then loads as now; a failure shows the error with Retry. For a manga not in
  the library the Categories button becomes **Add to library** (also on the MINUS menu); in the library the MINUS menu gains
  **Remove from library...**. Reading works as for any manga (History already shows manga outside the library, WP-15).
- **Adding** (`LibraryAdder`, a small model all three places use: preview, Browse's and Search all's Y menu):
  1. **Same title:** `libraryDuplicates(title)` (the manga's own id left out). Found: "You have an entry in your library with
     the same name: <title> (<source>)" → Show entry (opens that manga's details), Add anyway, Cancel.
  2. **Categories:** `askCategories` and the user has categories besides Default → the picker in **choose mode** (WP-24's
     seam): "Add <title> to", the "default"-marked categories (not id 0) ticked, a **Don't ask again** check, **Add** (X /
     button) instead of Save, B cancels. Otherwise the "default"-marked ones, or none (= Default), without a question.
     "Don't ask again" + Add sets `askCategories` false; Settings → "Ask for categories when adding" sets it back.
  3. **Details first:** a manga not `initialized` (added from a grid without opening it) gets `refreshManga` first, so the
     library cell has its details and chapters.
  4. **`addToLibrary(id, categories)`**: one `AddToLibrary` document (updateManga inLibrary true, then updateMangaCategories
     clear + add). Then the library is told (`LibraryModel::mangaAdded`: the shelves it joined are forgotten and load again
     when shown; the one on screen reloads keeping the focus; no category while Default isn't a tab → refresh), the grid
     it came from shows the "In library" tag, the preview shows "Categories: Reading".
- **Removing:** a confirm box "Remove <title> from your library? Reading progress stays on the server." → `RemoveFromLibrary`
  → `LibraryModel::mangaRemoved(id)` (it leaves every shelf, the focus keeps its index), Updates / History reload as after any
  screen closes, the preview shows Add to library again, the browse tag goes. The Library's Y menu: Open, Categories...,
  Remove from library....
- **Tick for new manga** (Categories screen, WP-24): the row menu gains "Tick for new manga" / "Don't tick for new manga"
  (`updateCategory` patch `default`), the row shows "Ticked for new manga". So the web UI's pre-ticks can be set from the
  Switch too.
- **Typing in tests:** the `text` command (WP-24) answers every askText (search, Search all, a Text filter).

## Read first
STATUS.md; this file; `src/ui/README.md`; `src/ui/library/*.hpp`, `src/ui/manga/*.hpp`, `src/ui/categories/*.hpp`,
`src/ui/main/MainActivity.hpp`, `src/ui/updates/ChapterRowsTab.hpp` (the label-on-the-first-row pattern),
`src/ui/components/CoverGrid.hpp`, `GridLayout.hpp`, `AskText.hpp`; `src/api/README.md` + `SuwayomiClient.hpp`;
`src/core/README.md`, `core/graphql/Operations.hpp`, `core/models/Source.hpp`, `Manga.hpp`, `Category.hpp`,
`core/LibraryTools.hpp`, `core/AppConfig.hpp`, `core/RuntimeProfile.hpp`; `app/TestHooks.hpp`; `tests/e2e/README.md`,
`tests/e2e/lib/test-source.ts`, `tools/gen-fixtures/test-source.ts` (the catalog, `selectManga()`, `listPage()`);
SUWAYOMI-API.md "Extensions, stores and online sources". The .cpp of the screens it changes (LibraryTab, MangaActivity,
MainActivity, CategoryPickerActivity, CategoriesActivity, SettingsTab).

## Deliverables
- [x] core: `SourceFilters.hpp` (the filter tree: kind, name, default, values, children; `readSourceFilters` with the S6
      limits; a state per filter; `filterChanges(filters, state)` → the server's change list, only what differs;
      `changedCount`; `resetFilters`), `SourceLanguages.hpp` (`languageName(code)`, Local's `localsourcelang`, the list order
      with Last used, hidden languages), `Source` gains `supportsLatest`, `isConfigurable`, `contentWarning`; `Manga::initialized`;
      `BrowsePage` (manga + `hasNextPage`); `AppConfig::browse`; profile `browseMaxItems` (1000), `sourcesMax` (500),
      `sourceFiltersMax` (300), `filterValuesMax` (500), `filterTextMaxChars` (64), `globalSearchConcurrency` (3),
      `globalSearchCovers` (5), `globalSearchSourcesMax` (100). Tests `test_core_browse.cpp`.
- [x] core GraphQL: documents `Sources`, `SourceFilters` (aliased defaults, two levels), `BrowseSource` (fetchSourceManga),
      `LibraryDuplicates` (likeInsensitive, escaped), `AddToLibrary`, `RemoveFromLibrary`, `SetCategoryDefault`; `Manga` and
      `FetchMangaAndChapters` + `initialized`; builders and readers; the source error's message cut ("Exception while
      fetching data (…) : " off, first line). Contract test green; recorded answers (`tools/record-fixtures`) for the new ones.
- [x] api: `sources()`, `sourceFilters(sourceId)`, `browseSource(sourceId, kind, page, query, changes)`,
      `libraryDuplicates(title, exceptId)`, `addToLibrary(mangaId, categoryIds)`, `removeFromLibrary(mangaId)`,
      `setCategoryDefault(id, on)`; mutations (`fetchSourceManga` is one) never repeated. Tests in `test_api_client.cpp`.
- [x] ui models (`GlobalSearchModel`: WP-14b, done 2026-11-19, with `LanguagesModel` and `CategoriesModel::setTickForNew`): `SourcesModel`, `SourceBrowseModel` (modes, paging, dedupe, the no-new-page stop, the limit, Failed + retry,
      more failed), `SourceFiltersModel` (rows with open groups, edits, apply, reset), `GlobalSearchModel` (the queue at the
      concurrency, states, cancel), `LibraryAdder` (duplicate → categories → details → add; remove), `LibraryModel::mangaAdded`
      / `mangaRemoved`, `MangaModel` (load from source first; in-library state), `CategoryPickerModel` choose mode,
      `GridLayout` neighbour with a focusable check. Tests: new `test_ui_browse.cpp` (each model: loading, Failed + retry, busy,
      destroyed while loading, every branch of the add flow), additions to `test_ui_library.cpp`, `test_ui_manga.cpp`,
      `test_ui_categories.cpp`, the GridLayout tests.
- [x] ui views (GlobalSearchActivity, LanguagesActivity, CategoriesActivity's Tick: WP-14b, done 2026-11-19): BrowseTab (replaces the placeholder), SourceActivity, FiltersActivity, GlobalSearchActivity,
      LanguagesActivity, `components/CoverCell` (+ tag), MangaActivity (Add / Remove, loading from source), LibraryTab (Y:
      Remove), CategoryPickerActivity (choose mode, Don't ask again), CategoriesActivity (Tick for new manga), SettingsTab (Ask
      for categories when adding). Layouts in `resources/xml/`, texts in `resources/i18n/en-US/`. Touch targets ≥ 72 px, every
      action also by touch (D16).
- [x] Test events (`global_search`, `languages`: WP-14b, done 2026-11-19): `sources` (rows: id, name, lang, nsfw, the label; last used; hidden), `browse` (source, mode, query,
      filters changed, items, hasNext, state), `filters` (rows: kind, name, value, depth, open; changed), `global_search`
      (query; per source: id, state, found, shown), `library_add` / `library_remove` (manga, ok, categories, duplicate,
      asked), `category_picker` gains `mode`, `languages` (rows).
- [x] Test source (test infrastructure): SNX Mature's third manga is renamed "Paper Garden" (the title of SNX Test's en-14),
      the same work in two sources, for the same-title warning; the gen-fixtures unit tests and the WP-23 live check stay green
      (update what they predict). `tests/e2e/lib/test-source.ts` gains `removeTestSourceMangaFromLibrary()` (every test-source
      manga out of the library, categories cleared: the scenario's known state).
- [x] Integration (`tests/integration/`, all 3 auth modes): sources (Local + 2), SNX Test's filters read into the tree (8,
      two groups), popular paging 25 / 25 / 10, latest's first key as `listPage` says, search "comet" (3), one filter of each
      type narrows as the catalog predicts, a source failure (switch file) comes back as a Server error with the short message,
      add with two categories → a query shows exactly those, the duplicate check finds a same-title library manga, remove →
      not in the library; it cleans up after itself.
- [x] Desktop + Switch e2e `browse` (below; WP-14b's steps green on both 2026-11-19).
- [x] Docs: SUWAYOMI-API.md (the facts above), ui / core / api READMEs, TESTING.md (scenario, the Mature rename),
      `tests/e2e/README.md` if a new helper pattern comes up, CHANGELOG.md, D56 (as built), ROADMAP (row done), STATUS.

## The scenario `browse` (desktop + Switch)
Known state first and at the end, also after a failure: `resetTestSource()`, `removeTestSourceMangaFromLibrary()`, WP-24's
`restoreSeed()`, `browse` back to its defaults in the test config. Then:
1. Browse tab → the list: Last used none, Local, English (SNX Test), Japanese (SNX Test) (golden `sources`). Languages: hide
   Japanese → its row goes; show it again.
2. SNX Test (EN) → Popular: 25, scroll down → 50 → 60, no more (`browse` events; keys as `listPage` predicts). Latest: the
   first key as predicted. Popular again.
3. Search "comet" → 3 (golden `source-search`); Clear → popular.
4. Filters (golden `filters` with Genres open): Status = Completed, Genres → Action include, Type → Webtoon ticked, Sort by
   Title ↑ → Apply → the manga `selectManga()` predicts, in that order; Reset → Apply → popular.
5. The switch file fails en `search` → search "x" → the error "SNX test failure: search", Retry after clearing it works.
6. "Comet Courier" → the preview loads from the source (the author shows) → Add to library → the picker in choose mode (golden
   `add-picker`) → tick Reading → Add → "Categories: Reading"; the server: in the library, categories exactly [Reading]. Back →
   its cover has the "In library" tag. The Library's Reading tab now has it.
7. "Paper Garden" (en-14) → Add with nothing ticked → Default. Install SNX Mature (`installExtension`), refresh the list: its
   row has "18+"; its "Paper Garden" → Add → the same-title box → Show entry opens SNX Test's Paper Garden; back; Add anyway
   → in the library twice. SNX Mature stays installed for step 8.
8. Search all "comet" with `delayMs` 2000 in the switch file (it slows every call): 4 sources, 3 at a time, so the first
   `global_search` event has 3 Searching and 1 Waiting. Done (golden `search-all`): Local Nothing found, SNX Mature (EN)
   Nothing found, SNX Test (EN) 3 found + 3 covers, SNX Test (JA) Nothing found (list order; the Last used row isn't searched
   twice). A on SNX Test (EN)'s card → the source's search with "comet". Clear the switch file, uninstall SNX Mature.
9. Remove "Comet Courier" from its preview's menu (desktop by the MINUS menu's touch button) → the confirm → not in the
   library (server), Add to library shows again.
10. Switch only (desktop can't press Y or MINUS): Y on a browse cover → Add to library... (Don't ask again ticked) → the next
    Y add goes straight to the "default"-marked category (mark "Long series" first by the Categories screen's Tick for new
    manga); Settings turns asking back on; Y on a library cover → Remove from library...; L / R between Popular and Latest.
11. Memory: 10 rounds of opening SNX Test (EN) and scrolling to the end: + < 16 MiB.

## Acceptance
- `tools/build.sh tests` (unit + contract), `npm test` + `npm run typecheck`, the WP-23 live check, `tools/build.sh
  integration` (3 modes), all desktop e2e green (`library-browse`, `library-tools`, `manga-details`, `updates-history`,
  `touch-pass` still pass with the new Y menu entry and the Browse tab), `tools/build.sh switch` + gate PASS.
- Switch (application mode, handheld, stock clocks): `browse` green, and `smoke`, `library-browse`, `library-tools`,
  `manga-details`, `touch-pass`, `updates-history` green. Numbers (D56, the app's clock from a played tap like D54): the
  sources list on screen ≤ 1 s after the tab opens; a source's first page in the grid ≤ 2 s (covers may follow); the next
  page ≤ 2 s after the grid asks; the filters screen ready ≤ 0.5 s; Search all over the 3 sources done ≤ 3 s; an add (after
  Add) and a remove (after the confirm) answered ≤ 1 s; memory as in step 11.

## Split point
If it runs long: **Search all** (`GlobalSearchModel`, its screen, the 6-column grid and `focusable`) moves to WP-14b, with
**Tick for new manga** and the **Languages** checklist (the list then shows every language). The sources list, a source's
screen with search and filters, the preview, add (with the warning and the picker) and remove stay: WP-16 builds on them.

## Sonnet-safe chunks (Opus WPs)
The language-name table in `SourceLanguages` (data: codes → English names, with a test that every code is unique);
LanguagesActivity's view once Opus has written its model, with CategoryPickerActivity as the example; the SUWAYOMI-API.md text.
Opus writes the filter rules, the models, the add flow, the grid change, the views with focus logic and the scenario, and
reviews the rest.
