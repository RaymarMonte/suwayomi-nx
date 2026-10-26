#pragma once

#include <string>
#include <utility>

#include "core/Result.hpp"

// How an image load can fail (WP-07). Errors are values: nothing in the image module throws across its boundary.
namespace snx::image {

enum class ImageErrorKind {
    Network,      // no answer from the server (refused, dropped, bad URL)
    Timeout,      // the download took longer than httpImageTimeoutMs
    Cancelled,    // the caller's CancelToken fired, or the loader shut down. Never shown: the screen is gone
    Http,         // the server answered, but not with the image (httpStatus says what: 404, 401, 500, ...)
    TooLarge,     // the file is over maxImageBytes, a side is over maxImageSide, or decoding it would need more than
                  // maxDecodeBytes (SAFETY.md S6: checked from the header, before anything big is allocated)
    Unsupported,  // not JPEG, PNG, WebP or GIF (or an animated WebP)
    Corrupt,      // looks like an image, but the data is broken, cut short or empty
    Busy,         // too many loads waiting already (imageQueueLimit); try again later
    Paused,       // memory is low, so prefetching is paused (the memory-pressure guard); ask again when it's needed
};

// "network", "timeout", "cancelled", "http", "too-large", "unsupported", "corrupt", "busy", "paused".
const char* imageErrorKindName(ImageErrorKind kind);

struct ImageError {
    ImageErrorKind kind = ImageErrorKind::Corrupt;
    std::string message;  // plain words, one line, safe to log
    int httpStatus = 0;   // for Http
};

template <typename T>
using ImageResultOf = core::Result<T, ImageError>;

inline ImageError imageError(ImageErrorKind kind, std::string message, int httpStatus = 0) {
    return ImageError{kind, std::move(message), httpStatus};
}

}  // namespace snx::image
