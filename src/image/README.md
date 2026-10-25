# image

**Purpose:** turn image bytes into textures safely: sniff the type, check the header against size limits (SAFETY.md S6), decode
JPEG/PNG/WebP at reduced scale, downscale to the screen, split tall images into tiles ≤ 2048 px, and cache them in memory and on
the SD card (ARCHITECTURE.md "image", D6).

**Public headers** (include as `"image/<Name>.hpp"`):
- `Image.hpp`: `moduleName()`, a placeholder so the module exists and links.

**Depends on:** `core`. From WP-07: `net` for fetching, and libjpeg-turbo, libpng, libwebp.

**Threading:** from WP-07: decoding runs on worker threads (count from `RuntimeProfile`). Textures are created and destroyed on the
main thread only. Every load takes a cancellation token.

**Tests:** `tests/unit/test_image.cpp` (doctest suite `image`). Later fuzz targets for sniff + decode (S6). Coverage target ≥ 60% lines.

**Status:** placeholder from WP-01. The real module is WP-07.
