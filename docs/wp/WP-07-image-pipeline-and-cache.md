# WP-07 image: decode, downscale, tile, caches, memory budget

Model: **Opus** · Size: L (split into 07a decode+tiles / 07b caches if needed) · Needs: WP-05 · Owner: –
Status: **done** (2026-10-26, D39). Benchmark numbers: D39 and STATUS.md.

## Goal
Load any page or cover image fast and safely within a fixed memory budget: fetch → sniff → decode at reduced scale →
downscale → tile → hand to UI. Memory and SD caches. This is the part most likely to cause crashes on the Switch, so it gets the most care.

## Read first
`docs/ARCHITECTURE.md` (image, memory, threading), STATUS.md memory facts from WP-00, `src/net/README.md`.

## Deliverables
- [x] Type sniffing (magic bytes, not Content-Type): JPEG, PNG, WebP, GIF (first frame), unknown → error
- [x] **Decompression-bomb guard** (SAFETY.md S6): read dimensions from the header first. Reject a side > 16384 px or `w×h×4` > the profile's max decode bytes
      *before* allocating. Fuzz targets for sniff + every decoder (ASan/UBSan)
- [x] GPU budget check before creating textures (S7). Disk-cache writes through `SafeFs` with the free-space guard and write-once files (S5)
- [x] Decoders: libjpeg-turbo (with DCT scaling 1/2, 1/4, 1/8), libpng or stb_image, libwebp (with scaled decode). Same libs on desktop and Switch
- [x] Target-box downscale (area/box filter, good quality for text)
- [x] Tiler: tall/wide images → tiles ≤ 2048 px, with a plan computed in `core` (unit-tested math)
- [x] `DecodedImage` RGBA with byte size accounting. Memory LRU cache with budget from `RuntimeProfile` (`Full`, D21)
- [x] Profile limits: decode threads, max decode resolution, tile window (ARCHITECTURE "Launch modes" table, `Full` column)
- [x] Memory-pressure guard: periodic check, shrink caches to 50% and pause prefetch above 85%, `mem_pressure` event. Test with a fake memory source
- [x] Disk cache on SD/desktop data dir: compressed bytes, URL-hash keys, size budget, eviction, survives restart, tolerant of corrupt entries
- [x] `ImageLoader` API: `load(url, targetBox, priority, cancel) -> callback on main thread`. Dedupes concurrent requests for the same URL
- [x] Test-hook `image` events (decode ms, bytes, cache hit/miss)
- [x] Unit tests with fixtures: every format, huge webtoon strip (memory stays under budget), corrupt/zero-byte/1×1, cancel mid-decode, eviction order
- [x] Switch benchmark (via Hello app or a test .nro), **in application mode**: decode times for 1000×1500 JPEG and 800×12000 WebP, peak memory → STATUS.md.
      Adjust `makeRuntimeProfile` defaults if the numbers say so
- [x] `src/image/README.md` with the budget numbers

## Acceptance
All tests green under ASan + TSan on host. Switch benchmark recorded. No single decode exceeds the budget.

## Sonnet-safe chunks
- None for the pipeline itself. Maybe the disk-cache eviction unit tests from a spec Opus writes.
