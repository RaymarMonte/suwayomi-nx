# WP-24 Library tools: search, manage categories, move manga

Model: **Opus** · Size: M (split point below) · Needs: WP-12 (done) · Owner: – (answers given 2026-11-13; optional: try it on your server)

## Goal
After this WP the Library can do what the server's web page was needed for until now: **search the whole library by title**,
**manage categories** (add, rename, reorder, delete) and **put a manga in other categories** from the library grid and from
the details screen, through a **category picker** (a checklist) that WP-14 reuses when it adds a manga to the library.
The Library keeps its model and grid (D42).

## Owner's answers (2026-11-13)
1. **Search: the whole library, by the server.** Part of a title; every category at once; the Library shows the matches as a
   cover grid; clearing the search goes back to the categories. Title only (not the author).
2. **Category tools: from the Library.** A Categories screen, opened from the Library's MINUS menu and an "Edit" button on its
   bar. Default stays fixed (first, can't be renamed, moved or deleted, and isn't listed there).
3. **Deleting a category keeps its manga in the library.** They lose that category; a manga in no other category shows under
   Default (the server does this by itself). A confirm box first says how many manga the category holds.
4. **Y on a cover opens its menu** ("Categories...", "Open"). Sort leaves Y: the MINUS menu and the Sort button (amends D42's
   buttons, owner OK). Touch reaches a manga's categories through its details screen. One manga at a time (no select mode).

## Checked on the test server (v2.4.2366, 2026-11-13; goes into SUWAYOMI-API.md)
- **Search:** `mangas(condition: {inLibrary: true}, filter: {title: {includesInsensitive: $q}}, order: [{by: TITLE}, {by: ID}],
  first, offset)` works across categories ("alp" → Alpha; "ALPHA" too; "日本" / "テスト" find the Japanese title). It is the
  `LibraryByCategory` document with another `$filter`, so no new document. **Trap:** `%` and `_` are SQL wildcards ("%"
  matched all 157, "a_p" matched Alpha). A backslash escapes them ("\%", "a\_p" → 0): escape `\`, `%`, `_` with `\`.
- `createCategory(input: {name})` → `category { id name order default }`, appended at the end (order = last + 1). An exact
  duplicate name is refused ("'name' must be unique", with a long Java stack trace in the message), but **an empty name, a
  name of spaces and a name differing only in case are accepted**. **`updateCategory(input: {id, patch: {name}})` checks
  nothing**: it renamed a category to an existing name, and renamed Default (id 0). So the app checks names itself.
- `updateCategoryOrder(input: {id, position})` answers every category in the new order. `position` is 1-based among the
  categories after Default (Default keeps order 0); 0 is refused ("'order' must not be <= 0"), a position past the end moves
  it to the end, moving Default does nothing.
- `deleteCategory(input: {categoryId})` → `category { id }`, `mangas { id }` (the manga that were in it). Its manga stay in
  the library; one in no other category then has none (= Default). The others' `order` is renumbered 1..n. Deleting id 0
  answers `category: null` and does nothing.
- `updateMangaCategories(input: {id, patch: {addToCategories, removeFromCategories, clearCategories}})` → `manga { categories
  { nodes { id name } } }`. Adding 0 is ignored; an unknown id fails (a foreign-key error). `updateMangasCategories` (many
  manga) exists but isn't needed (one at a time).
- A category's `mangas { totalCount }` counts its library manga (Default: the ones in no category: 2 in the seed).

## Design
- **Search** is a mode of `LibraryModel`: `search(text)` / `clearSearch()`. The results are one more shelf (D42's paging,
  "a manga seen twice shows once", the sorts: title from the server, Unread / Last read load all results and sort). The
  category on screen and its focus stay as they were and come back on clear. Not kept in config.json. The text is trimmed,
  at most `RuntimeProfile::searchMaxChars` (64); empty = clear. The bar shows "Search: “alp”" and "Clear" in place of the
  category tabs; L/R do nothing then; B clears (the hint says so). Empty result: "No manga match “zzz”".
- **The Categories screen** (`ui/categories/CategoriesActivity` + `CategoriesModel`): a one-column `CoverGrid` of rows (name,
  "3 manga"), header "Categories" with an Add button. A or a tap on a row: its menu (Rename..., Move up, Move down,
  Delete...). Y: Add. L / R: move the row up / down. B: back. One action at a time; the list shows the server's answer.
  Names: trimmed, not empty, at most `categoryNameMaxChars` (64), not the same as another one ignoring case (the
  "Default" name too); the problem shows in a message box and nothing is sent. When it closes after any change, the Library
  loads everything again (`refresh()`, it keeps the category on screen if it still exists).
- **The picker** (`ui/categories/CategoryPickerActivity` + `CategoryPickerModel`): "Categories for <title>", a checklist of
  the categories (not Default; none checked = Default, the hint says so), A / tap toggles, "New category..." (Y) adds one
  (same name rules) and checks it, Save (X, button) sends only the difference (`addToCategories` = newly checked,
  `removeFromCategories` = newly unchecked) in one `updateMangaCategories`, B / Cancel leaves without saving. Nothing changed:
  Save just closes. It answers the manga's new categories to its caller. WP-14 reuses it in a "choose" mode (no save, it
  hands back the checked set); this WP leaves that seam in the model and its header, untested by a screen.
- **The details screen:** a "Categories: Reading, Long series" button (cut to its width; "Categories: Default" when none),
  also on the MINUS menu; it opens the picker with the categories it already has; after a save it shows the new ones and
  tells the library.
- **The library learns about a move** (`LibraryModel::mangaCategoriesChanged(mangaId, categories)`, from the picker's
  caller, also when the details screen was opened from Updates / History): the shelf on screen drops the manga if it left
  that category (focus stays at the same index), every other shelf is forgotten (loads again when opened), and when the
  manga now has no category while Default isn't a tab, everything loads again (the Default tab appears).
- **Typing in tests:** a new `{"cmd":"text","text":"..."}` command on the test build's command channel (WP-13's touch
  channel): the next keyboard request answers that text without opening the keyboard. The Switch can't type into the
  system keyboard, the desktop's dialog works with `typeText()`, so both drivers use the command and one desktop step uses
  the real dialog. All keyboard requests go through one helper (`ui/components/AskText`).

## Read first
STATUS.md; `src/ui/README.md`; `src/ui/library/*.hpp`, `src/ui/manga/*.hpp`; `src/api/README.md` + `SuwayomiClient.hpp`;
`src/core/README.md`, `core/graphql/Operations.hpp`, `core/models/Category.hpp`, `Manga.hpp`; `app/TestHooks.hpp` (the
command channel); `tests/e2e/README.md`. The .cpp of the screens it changes (LibraryTab, MangaActivity, MainActivity).

## Deliverables
- [x] core: `LibraryTools.hpp` (`likeEscape`, `trimText`, `utf8Length`, `checkCategoryName` → None / Empty / TooLong /
      Taken, `categoryChange(before, after)` → add / remove without Default, `categoryPosition(categories, id, step)` → the
      server's 1-based position or 0); `Category::mangaCount`; `RuntimeProfile::searchMaxChars`, `categoryNameMaxChars`.
      Tests `test_core_library_tools.cpp`.
- [x] core GraphQL: `gql::librarySearch` (LibraryByCategory with the title filter), documents `ManageCategories` (with
      counts), `CreateCategory`, `UpdateCategory`, `UpdateCategoryOrder`, `DeleteCategory`, `UpdateMangaCategories`, their
      builders and readers (the server's long error text is cut as today). Contract test green; recorded answers or
      hand-built ones in the tests.
- [x] api: `searchLibrary(text, page)`, `manageCategories()`, `createCategory(name)`, `renameCategory(id, name)`,
      `moveCategory(id, position)`, `deleteCategory(id)`, `setMangaCategories(mangaId, add, remove)` (mutations never
      repeated). Tests in `test_api_client.cpp`.
- [x] ui models: `LibraryModel` search + `mangaCategoriesChanged`; `CategoriesModel`; `CategoryPickerModel` (with the choose
      seam). Tests `test_ui_library.cpp`, new `test_ui_categories.cpp` (loading, Failed + retry, every action, busy,
      name problems, destroyed while loading).
- [x] ui views: LibraryTab (Search / Edit buttons, the search bar, Y cover menu, B clears, MINUS menu: Go to category, Sort,
      Search, Categories, Refresh), CategoriesActivity, CategoryPickerActivity, MangaActivity's Categories button + MINUS
      entry, `AskText`. Layouts in `resources/xml/`, texts in `resources/i18n/en-US/`. Touch targets ≥ 72 px (D16).
- [x] Test events: `library` gains `search`; `categories` (the rows); `category_picker` (rows, checked, state);
      `category_action` (what, ok, problem). The `text` command in TestHooks (test builds only).
- [x] Integration (`tests/integration/`, all 3 auth modes): search (escaping), create → rename → move → set a manga's
      categories → delete, each checked by a query; it cleans up after itself.
- [x] Desktop + Switch e2e `library-tools`: a known state first (`lib/categories.ts`: the seed's categories, order and
      memberships back by GraphQL), search "alp" → Alpha (golden `search-results`), "zzz" → empty, Clear; Edit →
      Categories (golden `categories`), Add "Test shelf" (the real desktop dialog once), Rename, a taken name refused, Move up,
      Delete with its confirm; back → the Library has the tabs again; Alpha's details → Categories → check Long series →
      Save → the button shows both, the Long series tab has Alpha (golden `picker`). Switch only: Y on a cover → Categories...
      → uncheck → Save; MINUS → Search; the numbers below. The known state again at the end.
- [x] Docs: SUWAYOMI-API.md (the facts above), ui / core / api READMEs, TESTING.md (scenario, the `text` command),
      `tests/e2e/README.md` (typing on the Switch), CHANGELOG.md, D54, ROADMAP (row done), STATUS.

## Acceptance
- `tools/build.sh tests` (unit + contract), `npm test` + `npm run typecheck`, `tools/build.sh integration` (3 modes), all
  desktop e2e green (`library-browse`, `manga-details`, `touch-pass`, `updates-history` still pass with Y no longer Sort),
  `tools/build.sh switch` + gate PASS.
- Switch (application mode, handheld, stock clocks): `library-tools` green, and `library-browse`, `manga-details`,
  `touch-pass`, `updates-history` green. Numbers (D54): search results on screen ≤ 1.5 s after the text is in (157 manga),
  the picker ready ≤ 1 s, each category action answered ≤ 1 s, memory flat (+< 16 MiB) after 10 rounds of Categories
  screen open / close.

## Notes from building (2026-11-13)
- Not split: the Categories screen is in. The e2e golden is `search` (not `search-results`); B ends the search in the
  scenario (Clear is the same call). The details screen's button text ("Categories: a, b") has no event; the picker golden
  and the server checks cover the save. The Long series tab is checked by its count (2 → 3 → 2 on the Switch).
- borealis's TabFrame registers its own B on a tab's root view after creating it, which replaced LibraryTab's: the
  clear-search B sits on the bar and the body, added only while a search shows (an unavailable action shows greyed in the
  hints instead of "Back"), and clears on the next frame (it removes itself).
- Known gap: a picker save from a details screen opened from Updates or History doesn't tell the library; the library
  shows it after the next refresh (X).

## Split point
If it runs long: **the Categories screen** (add, rename, reorder, delete) moves to WP-24b. Search, the picker and Y on a
cover stay (WP-14 needs the picker).

## Sonnet-safe chunks (Opus WPs)
The Categories screen's view (`CategoriesActivity`, its XML and texts) once Opus has written `CategoriesModel` and its
tests, with the Library tab as the example. Opus reviews it.
