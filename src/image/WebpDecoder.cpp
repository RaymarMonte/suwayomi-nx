// WebP through libwebp 1.6.0, built from the pinned submodule library/libwebp on both platforms (D39: devkitPro's Switch
// package is 1.0.2, which has the CVE-2023-4863 heap overflow). libwebp scales while it decodes, so a big page never exists at
// full size (lossy files; a lossless one is decoded at full size inside libwebp first, which the size check counts).
// The data goes in piece by piece (incremental decoding), so a cancel stops it between pieces.

#include <algorithm>
#include <cstddef>
#include <new>
#include <string>

#include <webp/decode.h>

#include "image/Decoders.hpp"

namespace snx::image::detail {

namespace {

// How much data libwebp gets per step between cancel checks.
constexpr std::size_t kPieceBytes = 16 * 1024;

const char* statusText(VP8StatusCode status) {
    switch (status) {
        case VP8_STATUS_OK: return "ok";
        case VP8_STATUS_OUT_OF_MEMORY: return "out of memory";
        case VP8_STATUS_INVALID_PARAM: return "invalid parameter";
        case VP8_STATUS_BITSTREAM_ERROR: return "broken data";
        case VP8_STATUS_UNSUPPORTED_FEATURE: return "unsupported feature";
        case VP8_STATUS_SUSPENDED: return "data cut short";
        case VP8_STATUS_USER_ABORT: return "aborted";
        case VP8_STATUS_NOT_ENOUGH_DATA: return "data cut short";
    }
    return "unknown problem";
}

const std::uint8_t* data(std::string_view bytes) {
    return reinterpret_cast<const std::uint8_t*>(bytes.data());
}

}  // namespace

ImageResultOf<ImageHeader> webpHeader(std::string_view bytes) {
    WebPBitstreamFeatures features;
    const VP8StatusCode status = WebPGetFeatures(data(bytes), bytes.size(), &features);
    if (status != VP8_STATUS_OK) {
        return imageError(ImageErrorKind::Corrupt, std::string("broken WebP header: ") + statusText(status));
    }
    ImageHeader header;
    header.format = ImageFormat::Webp;
    header.size = {features.width, features.height};
    header.hasAlpha = features.has_alpha != 0;
    header.animated = features.has_animation != 0;
    header.lossless = features.format == 2;
    // A lossless image is decoded at full size into 32-bit pixels first; a lossy one with alpha keeps a full-size alpha plane.
    const std::uint64_t pixels = static_cast<std::uint64_t>(std::max(features.width, 0)) *
                                 static_cast<std::uint64_t>(std::max(features.height, 0));
    header.extraDecodeBytes = header.lossless ? pixels * 4 : (header.hasAlpha ? pixels : 0);
    return header;
}

ImageResultOf<RgbaImage> decodeWebp(std::string_view bytes, core::ImageSize output, const net::CancelToken& cancel) {
    WebPDecoderConfig config;
    if (WebPInitDecoderConfig(&config) == 0) {
        return imageError(ImageErrorKind::Unsupported, "libwebp version mismatch");
    }
    const VP8StatusCode features = WebPGetFeatures(data(bytes), bytes.size(), &config.input);
    if (features != VP8_STATUS_OK) {
        return imageError(ImageErrorKind::Corrupt, std::string("broken WebP header: ") + statusText(features));
    }
    if (config.input.has_animation != 0) {
        return imageError(ImageErrorKind::Unsupported, "animated WebP images aren't supported");
    }
    RgbaImage out;
    out.size = output;
    out.hasAlpha = config.input.has_alpha != 0;
    const std::size_t stride = static_cast<std::size_t>(output.width) * 4;
    try {
        out.pixels.resize(stride * static_cast<std::size_t>(output.height));
    } catch (const std::bad_alloc&) {
        return imageError(ImageErrorKind::TooLarge, "not enough memory to decode the WebP");
    }
    if (output.width != config.input.width || output.height != config.input.height) {
        config.options.use_scaling = 1;
        config.options.scaled_width = output.width;
        config.options.scaled_height = output.height;
    }
    config.options.use_threads = 0;
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = out.pixels.data();
    config.output.u.RGBA.stride = static_cast<int>(stride);
    config.output.u.RGBA.size = out.pixels.size();

    WebPIDecoder* decoder = WebPIDecode(nullptr, 0, &config);
    if (decoder == nullptr) {
        return imageError(ImageErrorKind::TooLarge, "not enough memory to decode the WebP");
    }
    VP8StatusCode status = VP8_STATUS_SUSPENDED;
    bool cancelled = false;
    for (std::size_t given = 0; given < bytes.size() && status == VP8_STATUS_SUSPENDED;) {
        if (cancel.cancelled()) {
            cancelled = true;
            break;
        }
        given = std::min(bytes.size(), given + kPieceBytes);
        // WebPIUpdate reads from our buffer (all of it so far) without copying it.
        status = WebPIUpdate(decoder, data(bytes), given);
    }
    WebPIDelete(decoder);
    WebPFreeDecBuffer(&config.output);  // nothing to free with external memory, but it's the documented way to finish
    if (cancelled) {
        return imageError(ImageErrorKind::Cancelled, "cancelled");
    }
    if (status != VP8_STATUS_OK) {
        return imageError(ImageErrorKind::Corrupt, std::string("broken WebP: ") + statusText(status));
    }
    return out;
}

}  // namespace snx::image::detail
