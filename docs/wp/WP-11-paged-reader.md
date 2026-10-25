# WP-11 Paged reader + progress sync

Model: **Opus** · Size: L (split 11a view+navigation / 11b prefetch+sync if needed) · Needs: WP-10 · Owner: –

## Goal
The core experience: open a chapter, show pages one at a time (LTR/RTL), fit modes, fast page turns via prefetch,
reading progress saved to the server, and seamless move to the next/previous chapter.

## Read first
`docs/ARCHITECTURE.md` (image, threading, memory), `src/image/README.md`, `src/api/README.md`, `src/core` reader-math headers.

## Deliverables
- [ ] `ReaderModel`: page list from `fetchChapterPages`, current index, direction, fit mode, prefetch window from `RuntimeProfile`
      (`Full`: −1/+3 plus next chapter's first pages),
      chapter transitions, progress debouncer (send `lastPageRead` ≤ every 5 s, on exit, on chapter end → `isRead=true`). Unit-tested
- [ ] Reader view: full-screen page (tiles for tall pages), fit width/height/screen, background colour, page counter overlay (toggle)
- [ ] Controls: L/R + D-pad left/right turn pages (respect RTL), A toggles overlay, B exits (saves progress), − opens reader settings
- [ ] Touch (D16): tap the left/right edge = previous/next page (mirrored for RTL), tap the centre = overlay, swipe left/right turns pages, overlay buttons for exit and reader settings. Leave room for pinch/drag in WP-13. Measure page-turn time at stock clocks (D17)
- [ ] Prefetch + cancellation outside the window. Memory stays within budget (`mem`/`image` events)
- [ ] Loading per page (spinner + percentage if available), per-page error with retry. Broken pages don't block navigation
- [ ] "Next chapter" / "Previous chapter" end screens
- [ ] Keep the screen awake while reading (platform hook). Restore on exit
- [ ] Reader prefs per manga (direction, fit) stored in config (or server `meta` later)
- [ ] Scenarios: `reader-paged` (colour-code verifies each page), `reader-rtl-spread`, `resume-progress`, `broken-images`; device run

## Acceptance
Page turn shows the next page in < 150 ms when prefetched (measured from events on the device, application mode).
Progress round-trips through the server. Device E2E green. Memory flat across 100 page turns.

## Sonnet-safe chunks
- None for the reader core. Reader-settings menu UI could be, once its model exists.
