#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "core/ImageGeometry.hpp"
#include "core/RuntimeProfile.hpp"
#include "image/ImageError.hpp"
#include "net/CancelToken.hpp"

// Bytes in, RGBA pixels out (WP-07, D39): sniff the type from the first bytes, read the header, check it against the limits
// (SAFETY.md S6) before anything big is allocated, decode at a reduced size where the format allows it, then shrink the rest
// of the way with a box filter. Runs on the calling thread (the loader's decode threads). Never throws.
namespace snx::image {

enum class ImageFormat { Unknown, Jpeg, Png, Webp, Gif };

// "jpeg", "png", "webp", "gif" or "" (unknown).
const char* imageFormatName(ImageFormat format);

// From the magic bytes at the start, never from a Content-Type header (servers and scrapers get those wrong).
ImageFormat sniffFormat(std::string_view bytes);

struct ImageHeader {
    ImageFormat format = ImageFormat::Unknown;
    core::ImageSize size;               // as stored; for a GIF the canvas its first frame is drawn on
    bool hasAlpha = false;
    bool progressive = false;           // a JPEG with several scans: libjpeg keeps all its coefficients while decoding
    bool lossless = false;              // a lossless WebP: libwebp decodes it at full size first
    bool animated = false;              // an animated WebP (not supported). A GIF animation is fine: its first frame shows
    std::uint64_t extraDecodeBytes = 0; // what the decoder needs on top of its output (the two cases above)
};

// Only the header: cheap, allocates nothing big. Corrupt for an empty or broken header, Unsupported for other formats.
ImageResultOf<ImageHeader> readHeader(std::string_view bytes);

// Decoded pixels: 4 bytes per pixel (R, G, B, A; alpha not premultiplied), rows top to bottom, width * 4 bytes each.
struct RgbaImage {
    core::ImageSize size;
    std::vector<std::uint8_t> pixels;
    bool hasAlpha = false;  // false: every A is 255
};

struct DecodeLimits {
    int maxSide = 16384;                // RuntimeProfile::maxImageSide
    std::uint64_t maxDecodeBytes = 0;   // RuntimeProfile::maxDecodeBytes: everything one decode has alive at once
    int shrinkSlackPercent = 0;         // RuntimeProfile::shrinkSlackPercent (0: always shrink to the box)
};
DecodeLimits decodeLimitsFrom(const core::RuntimeProfile& profile);

// What decodeImage did, for test events and the benchmark.
struct DecodeInfo {
    ImageHeader header;
    core::DecodePlan plan;
    std::uint64_t memUsedAtPeak = 0;  // the probe's answer while both buffers were alive (0 without a probe)
};

// The allocator's in-use bytes right now (platform::runtimeInfo().usedMem), or nothing.
using MemoryProbe = std::function<std::uint64_t()>;

// Decodes the image so that it fits `box` (a side of 0 = no limit that way), never bigger than the image itself and never
// needing more than limits.maxDecodeBytes (core::planDecode). The cancel token is checked between rows: a cancelled decode
// ends early with Cancelled (a PNG only before and after its decode).
ImageResultOf<RgbaImage> decodeImage(std::string_view bytes, core::ImageSize box, const DecodeLimits& limits,
                                     const net::CancelToken& cancel, DecodeInfo* info = nullptr,
                                     const MemoryProbe& probe = {});

// The box filter (area average: every source pixel counts by how much of it a target pixel covers), good for text and line
// art. Alpha is weighted, so transparent pixels don't darken the edges. `size` should not be bigger than the source.
ImageResultOf<RgbaImage> shrinkImage(const RgbaImage& source, core::ImageSize size, const net::CancelToken& cancel);

}  // namespace snx::image
