# WP-10 Manga details + chapter list

Model: **Opus** · Size: M · Needs: WP-09 · Owner: –

## Goal
Details screen: cover, title, author, status, description, genres. A chapter list with read/unread/progress/bookmark,
plus chapter actions. "Continue reading" starts the right chapter at the right page.

## Read first
`src/ui/README.md` (list-screen recipe), `src/api/README.md`, `src/core/models/Chapter.hpp`.

## Deliverables
- [ ] `MangaModel`: details + chapters, sort (source order asc/desc), filter (unread/bookmarked), continue-reading logic
      (first unread, or the in-progress one). Unit-tested thoroughly (edge cases: all read, none read, gaps, 300 chapters)
- [ ] Header view + recycling chapter list (name, number, date, read state, `lastPageRead/pageCount`)
- [ ] Actions: mark read/unread (single + "all previous"), bookmark, refresh chapters from source (`fetchMangaAndChapters`)
- [ ] A on chapter → reader (stub until WP-11). "Continue" button
- [ ] Touch (D16): tap a chapter opens the reader. Every action (mark read, bookmark, refresh, sort/filter, Continue) is reachable by tap
- [ ] Long description expand/collapse
- [ ] Scenarios: open details, toggle read state (verify on server via GraphQL from harness), sort/filter; goldens

## Acceptance
Unit + desktop + device E2E green. Server state matches UI after toggles.

## Sonnet-safe chunks
- Extra `MangaModel` continue-reading edge-case tests from a table Opus starts.
