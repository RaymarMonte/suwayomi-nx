// Fuzz target (SAFETY.md S6, WP-07): any bytes as an image file. Sniffing, the header readers and every decoder (libjpeg-turbo,
// libpng, libwebp, our GIF decoder) plus the shrink step must never crash, never read or write out of bounds (ASan), and a
// decoded image must be exactly as big as it says and fit its box and the decode budget.
// Seeds: tests/fixtures/images/. Run: tools/build.sh fuzz
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

#include "core/ImageGeometry.hpp"
#include "image/Decode.hpp"

using namespace snx;

namespace {

// Small limits keep each run fast; the guard logic is the same at any size.
constexpr int kMaxSide = 4096;
constexpr std::uint64_t kMaxDecodeBytes = 16 * 1024 * 1024;

void check(std::string_view bytes, core::ImageSize box) {
    const image::DecodeLimits limits{kMaxSide, kMaxDecodeBytes};
    image::DecodeInfo info;
    const auto result = image::decodeImage(bytes, box, limits, net::CancelToken(), &info);
    if (!result.ok()) {
        return;
    }
    const image::RgbaImage& img = result.value();
    const bool sizeOk = img.size.width > 0 && img.size.height > 0 && img.pixels.size() == core::rgbaBytes(img.size);
    const bool boxOk = (box.width == 0 || img.size.width <= box.width) && (box.height == 0 || img.size.height <= box.height);
    if (!sizeOk || !boxOk || info.plan.workBytes > kMaxDecodeBytes || core::rgbaBytes(img.size) > kMaxDecodeBytes) {
        std::abort();
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);
    (void)image::readHeader(bytes);
    check(bytes, {0, 0});   // full size (within the budget)
    check(bytes, {97, 0});  // shrunk: the scaled decoders and the box filter
    return 0;
}
