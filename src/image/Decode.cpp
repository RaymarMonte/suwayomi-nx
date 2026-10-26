#include "image/Decode.hpp"

#include <exception>
#include <new>
#include <string>

#include "image/Decoders.hpp"

namespace snx::image {

namespace {

std::string mib(std::uint64_t bytes) {
    return std::to_string((bytes + core::kMiB - 1) / core::kMiB) + " MiB";
}

bool startsWith(std::string_view bytes, std::string_view magic) {
    return bytes.size() >= magic.size() && bytes.compare(0, magic.size(), magic) == 0;
}

core::DecodeScaling scalingOf(ImageFormat format) {
    switch (format) {
        case ImageFormat::Jpeg: return core::DecodeScaling::Dct;
        case ImageFormat::Webp: return core::DecodeScaling::Any;
        default: return core::DecodeScaling::None;
    }
}

ImageResultOf<RgbaImage> runDecoder(std::string_view bytes, const ImageHeader& header, const core::DecodePlan& plan,
                                    const DecodeLimits& limits, const net::CancelToken& cancel) {
    switch (header.format) {
        case ImageFormat::Jpeg: return detail::decodeJpeg(bytes, plan.denominator, limits.maxDecodeBytes, cancel);
        case ImageFormat::Png: return detail::decodePng(bytes, cancel);
        case ImageFormat::Webp: return detail::decodeWebp(bytes, plan.decoded, cancel);
        case ImageFormat::Gif: return detail::decodeGif(bytes, cancel);
        case ImageFormat::Unknown: break;
    }
    return imageError(ImageErrorKind::Unsupported, "not a JPEG, PNG, WebP or GIF image");
}

ImageResultOf<RgbaImage> decodeChecked(std::string_view bytes, core::ImageSize box, const DecodeLimits& limits,
                                       const net::CancelToken& cancel, DecodeInfo* info, const MemoryProbe& probe) {
    auto header = readHeader(bytes);
    if (!header.ok()) {
        return header.error();
    }
    const ImageHeader& h = header.value();
    if (info != nullptr) {
        info->header = h;
    }
    // SAFETY.md S6, the decompression-bomb guard: everything below is decided from the header, before any big allocation.
    if (h.size.width <= 0 || h.size.height <= 0) {
        return imageError(ImageErrorKind::Corrupt, "the image has no pixels");
    }
    if (h.size.width > limits.maxSide || h.size.height > limits.maxSide) {
        return imageError(ImageErrorKind::TooLarge, "the image is " + std::to_string(h.size.width) + "x" +
                                                        std::to_string(h.size.height) + " pixels; the most is " +
                                                        std::to_string(limits.maxSide) + " per side");
    }
    if (h.animated) {
        return imageError(ImageErrorKind::Unsupported, "animated WebP images aren't supported");
    }
    const core::DecodePlan plan = core::planDecode(h.size, box, scalingOf(h.format), limits.maxDecodeBytes, h.extraDecodeBytes,
                                                   limits.shrinkSlackPercent);
    if (info != nullptr) {
        info->plan = plan;
    }
    if (!plan.ok) {
        return imageError(ImageErrorKind::TooLarge, "decoding this image would need more than " + mib(limits.maxDecodeBytes));
    }
    if (cancel.cancelled()) {
        return imageError(ImageErrorKind::Cancelled, "cancelled");
    }
    auto decoded = runDecoder(bytes, h, plan, limits, cancel);
    if (!decoded.ok()) {
        return decoded;
    }
    RgbaImage& image = decoded.value();
    // Never trust a decoder's numbers blindly: the pixels must be exactly what the size says, and no bigger than planned.
    if (image.pixels.size() != core::rgbaBytes(image.size) ||
        core::rgbaBytes(image.size) > core::rgbaBytes(plan.decoded)) {
        return imageError(ImageErrorKind::Corrupt, "the decoder gave an unexpected size");
    }
    image.hasAlpha = image.hasAlpha || h.hasAlpha;
    const core::ImageSize outputSize = core::fitInside(image.size, plan.output);
    if (outputSize == image.size) {
        if (info != nullptr && probe) {
            info->memUsedAtPeak = probe();
        }
        return decoded;
    }
    auto output = shrinkImage(image, outputSize, cancel);
    if (info != nullptr && probe) {
        info->memUsedAtPeak = probe();  // the decoded and the shrunk image are both alive here: the peak
    }
    return output;
}

}  // namespace

const char* imageErrorKindName(ImageErrorKind kind) {
    switch (kind) {
        case ImageErrorKind::Network: return "network";
        case ImageErrorKind::Timeout: return "timeout";
        case ImageErrorKind::Cancelled: return "cancelled";
        case ImageErrorKind::Http: return "http";
        case ImageErrorKind::TooLarge: return "too-large";
        case ImageErrorKind::Unsupported: return "unsupported";
        case ImageErrorKind::Corrupt: return "corrupt";
        case ImageErrorKind::Busy: return "busy";
        case ImageErrorKind::Paused: return "paused";
    }
    return "unknown";
}

const char* imageFormatName(ImageFormat format) {
    switch (format) {
        case ImageFormat::Jpeg: return "jpeg";
        case ImageFormat::Png: return "png";
        case ImageFormat::Webp: return "webp";
        case ImageFormat::Gif: return "gif";
        case ImageFormat::Unknown: break;
    }
    return "";
}

ImageFormat sniffFormat(std::string_view bytes) {
    if (startsWith(bytes, std::string_view("\xFF\xD8\xFF", 3))) {
        return ImageFormat::Jpeg;
    }
    if (startsWith(bytes, std::string_view("\x89PNG\r\n\x1A\n", 8))) {
        return ImageFormat::Png;
    }
    if (startsWith(bytes, "GIF87a") || startsWith(bytes, "GIF89a")) {
        return ImageFormat::Gif;
    }
    if (bytes.size() >= 12 && startsWith(bytes, "RIFF") && bytes.compare(8, 4, "WEBP") == 0) {
        return ImageFormat::Webp;
    }
    return ImageFormat::Unknown;
}

ImageResultOf<ImageHeader> readHeader(std::string_view bytes) {
    if (bytes.empty()) {
        return imageError(ImageErrorKind::Corrupt, "the file is empty");
    }
    switch (sniffFormat(bytes)) {
        case ImageFormat::Jpeg: return detail::jpegHeader(bytes);
        case ImageFormat::Png: return detail::pngHeader(bytes);
        case ImageFormat::Webp: return detail::webpHeader(bytes);
        case ImageFormat::Gif: return detail::gifHeader(bytes);
        case ImageFormat::Unknown: break;
    }
    return imageError(ImageErrorKind::Unsupported, "not a JPEG, PNG, WebP or GIF image");
}

DecodeLimits decodeLimitsFrom(const core::RuntimeProfile& profile) {
    return DecodeLimits{profile.maxImageSide, profile.maxDecodeBytes, profile.shrinkSlackPercent};
}

ImageResultOf<RgbaImage> decodeImage(std::string_view bytes, core::ImageSize box, const DecodeLimits& limits,
                                     const net::CancelToken& cancel, DecodeInfo* info, const MemoryProbe& probe) {
    // Errors are values: nothing may escape to the worker thread (S1).
    try {
        return decodeChecked(bytes, box, limits, cancel, info, probe);
    } catch (const std::bad_alloc&) {
        return imageError(ImageErrorKind::TooLarge, "not enough memory to decode the image");
    } catch (const std::exception& e) {
        return imageError(ImageErrorKind::Corrupt, std::string("decoding failed: ") + e.what());
    } catch (...) {
        return imageError(ImageErrorKind::Corrupt, "decoding failed");
    }
}

}  // namespace snx::image
