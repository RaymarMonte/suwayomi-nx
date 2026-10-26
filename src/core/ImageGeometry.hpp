#pragma once

#include <cstdint>
#include <vector>

// The math behind image decoding and tiling (ARCHITECTURE.md "core" reader math, WP-07). Pure functions, so the image
// module's choices (how small to decode, how to cut tiles, which tiles to keep) are unit-tested without any decoder.
namespace snx::core {

struct ImageSize {
    int width = 0;
    int height = 0;
};

inline bool operator==(ImageSize a, ImageSize b) {
    return a.width == b.width && a.height == b.height;
}
inline bool operator!=(ImageSize a, ImageSize b) {
    return !(a == b);
}

// Bytes of an RGBA image of this size (4 bytes per pixel). 0 for an empty or negative size.
std::uint64_t rgbaBytes(ImageSize size);

// The biggest size that fits inside `box` and keeps the image's shape, but never bigger than the image itself (the GPU
// scales up for free, so decoding bigger only costs memory). A box side of 0 means "no limit that way". At least 1×1.
// An empty image gives {0, 0}.
ImageSize fitInside(ImageSize image, ImageSize box);

// fitInside(), then smaller still (same shape) until the RGBA bytes are at most maxBytes (SAFETY.md S6: no single decoded
// image over the profile's maxDecodeBytes).
ImageSize fitDecoded(ImageSize image, ImageSize box, std::uint64_t maxBytes);

// libjpeg can decode at 1/2, 1/4 or 1/8 of the size almost for free (DCT scaling): ceil(w/d) × ceil(h/d).
ImageSize scaledBy(ImageSize image, int denominator);

// How a decoder can get smaller while decoding.
enum class DecodeScaling {
    None,  // decodes at full size, then the result is shrunk (PNG, GIF)
    Dct,   // decodes at 1/1, 1/2, 1/4 or 1/8 (JPEG), then shrinks the rest of the way
    Any,   // decodes straight to any smaller size (WebP's built-in scaler)
};

struct DecodePlan {
    bool ok = false;              // false: no way to stay within the budget, so the image isn't decoded (TooLarge)
    int denominator = 1;          // Dct: decode at 1/denominator
    ImageSize decoded;            // what the decoder itself produces
    ImageSize output;             // the final image; == decoded when no shrink step is needed
    std::uint64_t workBytes = 0;  // everything big that is alive at once: extraBytes + decoded + output (if a shrink step)
};

// Plans one decode so it never needs more than maxBytes (SAFETY.md S6: "no single decode exceeds the budget").
// `extraBytes` are the decoder's own big buffers (a progressive JPEG's coefficients, a lossless WebP's full-size picture).
// The output fits the box (fitInside), never bigger than the image. When the budget is tight the output gets smaller
// instead; Dct decodes at a bigger reduction first, before giving up (its quality drops a little, it still shows).
// `slackPercent` (RuntimeProfile::shrinkSlackPercent): when what the decoder makes is at most that much bigger per side than
// the planned output, there is no shrink step and the output is the decoded image (slightly over the box).
DecodePlan planDecode(ImageSize image, ImageSize box, DecodeScaling scaling, std::uint64_t maxBytes,
                      std::uint64_t extraBytes = 0, int slackPercent = 0);

// One tile of an image, in pixels of the decoded image.
struct TileRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Cuts an image into tiles no side of which is bigger than maxSide (GPU textures, SAFETY.md S7). Tiles in a row or column are
// equal (the last one may be a few pixels smaller), ordered row by row from the top: a tall webtoon strip of 800×12000 with
// maxSide 2048 becomes 6 tiles of 800×2000. A small image is one tile. Empty image or maxSide < 1: no tiles.
std::vector<TileRect> planTiles(ImageSize image, int maxSide);

// Tiles [first, last) to keep decoded in a vertical reader.
struct TileRange {
    int first = 0;
    int last = 0;  // one past the last; first == last means none
};

// Which tiles a webtoon reader keeps decoded (RuntimeProfile::webtoonTilesAround, WP-11): the ones that overlap the view
// [viewTop, viewBottom) plus `around` more rows of tiles above and below. `tiles` must be sorted top to bottom (planTiles'
// order; tiles of one row share their y). With nothing in view, the nearest row counts as the view.
TileRange tileWindow(const std::vector<TileRect>& tiles, int viewTop, int viewBottom, int around);

}  // namespace snx::core
