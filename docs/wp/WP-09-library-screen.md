# WP-09 Library screen (categories + cover grid)

Model: **Opus** · Size: M · Needs: WP-07, WP-08 · Owner: –

## Goal
The main screen: category tabs/selector and a fast, recycling grid of covers with titles and unread badges,
lazy cover loading, pagination, and pull-to-refresh equivalent. This is the reference "list screen" that WP-14/15 copy.

## Read first
`src/ui/README.md` (screen pattern), `src/image/README.md`, `src/api/README.md`.

## Deliverables
- [x] `LibraryModel`: categories, per-category paginated manga, sort (title/unread/last read), unread counts. Unit-tested with FakeHttpClient
- [x] Recycling grid (borealis RecyclerFrame/Grid). Cover cells with placeholder → image, title (2 lines, ellipsis), unread badge
- [x] Cover loads: `Interactive` priority for visible cells, cancelled when scrolled away, sized to the cell
- [x] L/R switch category, Y sort menu, X refresh, MINUS opens a menu (PLUS stays "quit", D42), A opens manga (stub screen until WP-10)
- [x] Touch (D16): tap a cover opens the manga, drag/flick scrolls the grid, tap category tabs, sort and refresh reachable by tap (not only Y/X)
- [x] Empty library / empty category / error states
- [x] Remembers last category and scroll position
- [ ] Scenario `library-browse` (desktop + device), goldens for grid and empty state (desktop done; device run needs the console)
- [x] `src/ui/README.md` updated with "how to make a list screen" (the recipe WP-14/15 will follow)

## Acceptance
Smooth scroll through `Delta Big` + all fixtures. No visible cover loads for off-screen cells (check `image` events).
Device E2E green. Memory flat after scrolling the whole library 3× (`mem` events).
