// PNG through libpng's "simplified API" (png_image, libpng 1.6): it handles every colour type, bit depth, palette,
// transparency and interlacing itself, reports errors as return values (no setjmp on our side), and gives 8-bit RGBA.

#include <climits>
#include <cstring>
#include <new>
#include <string>

#include <png.h>

#include "image/Decoders.hpp"

namespace snx::image::detail {

namespace {

int toInt(png_uint_32 value) {
    return value > static_cast<png_uint_32>(INT_MAX) ? INT_MAX : static_cast<int>(value);
}

// Reads the header into `image`. False (and `image` freed) if it isn't a usable PNG.
bool begin(png_image& image, std::string_view bytes, std::string& message) {
    std::memset(&image, 0, sizeof image);
    image.version = PNG_IMAGE_VERSION;
    if (png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()) == 0) {
        message = std::string("broken PNG: ") + image.message;
        png_image_free(&image);
        return false;
    }
    return true;
}

}  // namespace

ImageResultOf<ImageHeader> pngHeader(std::string_view bytes) {
    png_image image;
    std::string message;
    if (!begin(image, bytes, message)) {
        return imageError(ImageErrorKind::Corrupt, message);
    }
    ImageHeader header;
    header.format = ImageFormat::Png;
    header.size = {toInt(image.width), toInt(image.height)};
    header.hasAlpha = (image.format & PNG_FORMAT_FLAG_ALPHA) != 0;
    png_image_free(&image);
    return header;
}

ImageResultOf<RgbaImage> decodePng(std::string_view bytes, const net::CancelToken& cancel) {
    png_image image;
    std::string message;
    if (!begin(image, bytes, message)) {
        return imageError(ImageErrorKind::Corrupt, message);
    }
    RgbaImage out;
    out.size = {toInt(image.width), toInt(image.height)};
    out.hasAlpha = (image.format & PNG_FORMAT_FLAG_ALPHA) != 0;
    image.format = PNG_FORMAT_RGBA;
    if (cancel.cancelled()) {
        png_image_free(&image);
        return imageError(ImageErrorKind::Cancelled, "cancelled");
    }
    try {
        out.pixels.resize(PNG_IMAGE_SIZE(image));
    } catch (const std::bad_alloc&) {
        png_image_free(&image);
        return imageError(ImageErrorKind::TooLarge, "not enough memory to decode the PNG");
    }
    const int done = png_image_finish_read(&image, nullptr, out.pixels.data(), 0, nullptr);
    if (done == 0) {
        message = std::string("broken PNG: ") + image.message;
    }
    png_image_free(&image);
    if (done == 0) {
        return imageError(ImageErrorKind::Corrupt, message);
    }
    if (cancel.cancelled()) {
        return imageError(ImageErrorKind::Cancelled, "cancelled");
    }
    return out;
}

}  // namespace snx::image::detail
