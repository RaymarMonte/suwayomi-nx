#pragma once

#include <cstdint>
#include <string_view>

#include "image/Decode.hpp"

// Inside the image module only: one header reader and one decoder per format. decodeImage() (Decode.cpp) checks the header
// against the limits and plans the size before it calls a decoder, so the decoders trust the size they are asked for, but
// still never read outside `bytes`.
namespace snx::image::detail {

// JPEG (libjpeg-turbo). Decodes at 1/denominator (1, 2, 4 or 8). maxMemory caps libjpeg's own buffers as a second line of
// defence. CMYK files are converted to RGB.
ImageResultOf<ImageHeader> jpegHeader(std::string_view bytes);
ImageResultOf<RgbaImage> decodeJpeg(std::string_view bytes, int denominator, std::uint64_t maxMemory,
                                    const net::CancelToken& cancel);

// PNG (libpng's simplified API: every colour type and bit depth, palettes, transparency, interlacing). Full size only.
ImageResultOf<ImageHeader> pngHeader(std::string_view bytes);
ImageResultOf<RgbaImage> decodePng(std::string_view bytes, const net::CancelToken& cancel);

// WebP (libwebp, lossy and lossless). Decodes straight to `output` with libwebp's own scaler.
ImageResultOf<ImageHeader> webpHeader(std::string_view bytes);
ImageResultOf<RgbaImage> decodeWebp(std::string_view bytes, core::ImageSize output, const net::CancelToken& cancel);

// GIF: the first frame only, drawn on the GIF's canvas (our own small decoder, GifDecoder.cpp). Full size only.
ImageResultOf<ImageHeader> gifHeader(std::string_view bytes);
ImageResultOf<RgbaImage> decodeGif(std::string_view bytes, const net::CancelToken& cancel);

// How often decoders look at the cancel token: every this many rows.
inline constexpr int kCancelCheckRows = 32;

}  // namespace snx::image::detail
