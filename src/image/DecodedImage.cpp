#include "image/DecodedImage.hpp"

#include <cstring>
#include <utility>

namespace snx::image {

DecodedImage::DecodedImage(RgbaImage image, int maxTextureSide, ImageFormat format, core::ImageSize sourceSize)
    : image_(std::move(image)), tiles_(core::planTiles(image_.size, maxTextureSide)), format_(format),
      sourceSize_(sourceSize) {}

std::uint64_t DecodedImage::bytes() const {
    return image_.pixels.size() + tiles_.size() * sizeof(core::TileRect) + sizeof(DecodedImage);
}

const std::uint8_t* DecodedImage::tilePixels(std::size_t index, std::vector<std::uint8_t>& scratch) const {
    if (index >= tiles_.size()) {
        return nullptr;
    }
    const core::TileRect& tile = tiles_[index];
    const std::size_t imageStride = static_cast<std::size_t>(image_.size.width) * 4;
    const std::uint8_t* start = image_.pixels.data() + static_cast<std::size_t>(tile.y) * imageStride +
                                static_cast<std::size_t>(tile.x) * 4;
    if (tile.width == image_.size.width) {
        return start;  // whole rows: already packed tightly
    }
    const std::size_t tileStride = static_cast<std::size_t>(tile.width) * 4;
    scratch.resize(tileStride * static_cast<std::size_t>(tile.height));
    for (int row = 0; row < tile.height; ++row) {
        std::memcpy(scratch.data() + static_cast<std::size_t>(row) * tileStride,
                    start + static_cast<std::size_t>(row) * imageStride, tileStride);
    }
    return scratch.data();
}

}  // namespace snx::image
