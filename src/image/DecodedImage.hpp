#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/ImageGeometry.hpp"
#include "image/Decode.hpp"

namespace snx::image {

// A finished image, ready for the screen (WP-07): RGBA pixels at the size it will be drawn, cut into tiles that each fit one
// GPU texture (no side over RuntimeProfile::maxTextureSide, SAFETY.md S7). Never changes once made, so any thread may read it;
// the caches and the UI share it through ImagePtr. The UI makes one texture per tile, on the main thread only.
class DecodedImage {
  public:
    DecodedImage(RgbaImage image, int maxTextureSide, ImageFormat format, core::ImageSize sourceSize);

    core::ImageSize size() const { return image_.size; }
    core::ImageSize sourceSize() const { return sourceSize_; }  // the image as it was stored
    ImageFormat format() const { return format_; }
    bool hasAlpha() const { return image_.hasAlpha; }

    // What it counts for in a memory cache: its pixels plus a little bookkeeping.
    std::uint64_t bytes() const;

    // The whole image: width * 4 bytes per row, top to bottom.
    const std::uint8_t* pixels() const { return image_.pixels.data(); }

    // Row by row from the top (core::planTiles). A small image is one tile.
    const std::vector<core::TileRect>& tiles() const { return tiles_; }

    // Tile `index`'s pixels packed tightly (tile width * 4 bytes per row), the way a texture upload wants them. When the tile
    // is as wide as the image (any tile of a tall strip) that's a pointer into the image, no copy; otherwise the tile is
    // copied into `scratch`. nullptr for an index out of range.
    const std::uint8_t* tilePixels(std::size_t index, std::vector<std::uint8_t>& scratch) const;

  private:
    RgbaImage image_;
    std::vector<core::TileRect> tiles_;
    ImageFormat format_;
    core::ImageSize sourceSize_;
};

using ImagePtr = std::shared_ptr<const DecodedImage>;

}  // namespace snx::image
