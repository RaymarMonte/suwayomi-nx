# image

**Purpose:** load any page or cover image fast and safely within a fixed memory budget (ARCHITECTURE.md "image", D6, D39):
memory cache → disk cache → download → sniff → header check (SAFETY.md S6) → decode at a reduced size → box-filter shrink →
tiles ≤ 2048 px → the answer on the main thread. The UI turns tiles into textures (main thread, `TextureBudget`, S7).

**Public headers** (include as `"image/<Name>.hpp"`):
- `ImageLoader.hpp`: `ImageLoader(http, loaderOptionsFrom(profile) + deliver/fs/memory/log)`, `load(LoadRequest{url, box,
  priority, kind}, cancel, done)` → `done(ImageResult)` on the main thread; `cached()`, `clearMemory()`, `shutdown()`, event and
  memory-pressure observers. Same URL at once = one download. `ImageKind` Page/Cover = two memory caches.
- `Decode.hpp`: `sniffFormat`, `readHeader`, `decodeImage(bytes, box, DecodeLimits, cancel)`, `shrinkImage`, `RgbaImage`.
- `DecodedImage.hpp`: the finished image (`ImagePtr`): size, source size, tiles, `tilePixels()` (no copy for full-width tiles).
- `Caches.hpp`: `MemoryCache`, `cacheKey`, `TextureBudget` + `TextureLease`. `DiskCache.hpp`: the SD cache. `WorkerPool.hpp`.
- `ImageError.hpp`: `ImageError{kind, message, httpStatus}`: Network, Timeout, Cancelled, Http, TooLarge, Unsupported, Corrupt,
  Busy, Paused. A load cancelled before delivery ends Cancelled: check that first and return.

**Budgets (Full profile on the console, 3,184 MiB heap; all from `RuntimeProfile`):** decoded pages 512 MiB, covers 64 MiB,
GPU textures 256 MiB, one decode ≤ 128 MiB (output + shrunk copy + the decoder's own buffers, planned from the header by
`core::planDecode`), 2 decode threads (1 MiB stacks) + 1 disk thread (256 KiB), at most 128 loads, downloads ≤ 32 MiB in 60 s,
sides ≤ 16384 px, decode ≤ 2× the screen (an image up to 15 % over its box isn't shrunk: the GPU scales it), tiles ≤ 2048 px,
disk cache 1 GiB, never below 1 GiB free on the SD card. Memory guard every 2 s: ≥ 85 % used → caches to 50 %, prefetch
paused, until < 75 %. Safe mode halves the memory budgets. **Switch (v1, stock clocks):** JPEG 1000×1500 page 18 ms, WebP strip
800×12000 176 ms, SD cache read 6–11 ms, the strip's peak +38.6 MB (`image-bench`, D39).

**Depends on:** `core`, `net` (HttpClient), `platform` (SafeFs for the disk cache). libjpeg-turbo + libpng (system / devkitPro),
libwebp 1.6.0 from `library/libwebp` (`git submodule update --init library/libwebp`), our GIF decoder. Never `api` or borealis.

**Threading:** `load()`/`cached()` from any thread; answers through `deliver` (the app: `brls::sync`). Observers run on loader
threads. Decoding on `decodeThreads` workers, disk I/O + timers on one disk thread, downloads on net's thread. Textures: main only.

**Tests:** `tests/unit/test_image.cpp` (formats, broken files, size guard, shrink, tiles, cancel, pool) and `test_image_loader.cpp`
(disk cache, loader with `FakeHttpClient`), fixtures in `tests/fixtures/images/`. Fuzz: `fuzz_image_decode`. Bench: `image-bench`.

**Status:** WP-07.
