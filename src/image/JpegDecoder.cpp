// JPEG through libjpeg-turbo (D39). libjpeg reports a fatal error through a callback that must not return, so the error
// callback jumps back (longjmp) to a setjmp() in the function that called libjpeg, instead of letting libjpeg call exit().
// That's the standard libjpeg pattern, with one C++ rule to keep: between setjmp() and the libjpeg calls that may jump, no
// local object with a destructor may be created. So the pixels live behind a unique_ptr made before setjmp(), and the
// libjpeg structs are plain C structs.

#include <climits>
#include <csetjmp>
#include <cstddef>
#include <cstdio>  // jpeglib.h needs FILE and size_t first
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include <jpeglib.h>

#include "image/Decoders.hpp"

namespace snx::image::detail {

namespace {

struct JpegErrors {
    jpeg_error_mgr base;  // first, so libjpeg's pointer to it is a pointer to all of this
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
    int warnings;  // "corrupt JPEG data", "premature end of data": libjpeg carries on with grey, we call it broken
};

void onJpegError(j_common_ptr cinfo) {
    auto* errors = reinterpret_cast<JpegErrors*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, errors->message);
    std::longjmp(errors->jump, 1);
}

void onJpegMessage(j_common_ptr cinfo, int level) {
    if (level >= 0) {
        return;  // trace output, not a problem
    }
    auto* errors = reinterpret_cast<JpegErrors*>(cinfo->err);
    if (errors->warnings++ == 0) {
        (*cinfo->err->format_message)(cinfo, errors->message);
    }
}

void prepare(jpeg_decompress_struct& cinfo, JpegErrors& errors) {
    std::memset(&cinfo, 0, sizeof cinfo);
    std::memset(&errors, 0, sizeof errors);
    cinfo.err = jpeg_std_error(&errors.base);
    errors.base.error_exit = onJpegError;
    errors.base.emit_message = onJpegMessage;
}

void setSource(jpeg_decompress_struct& cinfo, std::string_view bytes) {
    jpeg_mem_src(&cinfo, reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<unsigned long>(bytes.size()));
}

std::uint64_t roundUp(std::uint64_t value, std::uint64_t multiple) {
    return multiple == 0 ? value : (value + multiple - 1) / multiple * multiple;
}

// A JPEG with several scans (progressive, or one component per scan) keeps every DCT coefficient of the whole image while it
// decodes: libjpeg's coefficient buffer, 128 bytes per 8×8 block per component, at full size whatever the output scale.
std::uint64_t coefficientBytes(const jpeg_decompress_struct& cinfo) {
    if (!cinfo.progressive_mode && cinfo.comps_in_scan >= cinfo.num_components) {
        return 0;
    }
    std::uint64_t total = 0;
    for (int c = 0; c < cinfo.num_components; ++c) {
        const jpeg_component_info& component = cinfo.comp_info[c];
        total += roundUp(component.width_in_blocks, static_cast<std::uint64_t>(component.h_samp_factor)) *
                 roundUp(component.height_in_blocks, static_cast<std::uint64_t>(component.v_samp_factor)) * sizeof(JBLOCK);
    }
    return total;
}

bool isCmyk(const jpeg_decompress_struct& cinfo) {
    return cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK;
}

// CMYK rows (4 bytes per pixel, like RGBA) to RGBA in place. Adobe's files store CMYK inverted (the usual case).
void cmykToRgba(std::uint8_t* pixels, std::size_t count, bool adobeInverted) {
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* p = pixels + i * 4;
        unsigned c = p[0], m = p[1], y = p[2], k = p[3];
        if (!adobeInverted) {
            c = 255 - c;
            m = 255 - m;
            y = 255 - y;
            k = 255 - k;
        }
        p[0] = static_cast<std::uint8_t>(c * k / 255);
        p[1] = static_cast<std::uint8_t>(m * k / 255);
        p[2] = static_cast<std::uint8_t>(y * k / 255);
        p[3] = 255;
    }
}

}  // namespace

ImageResultOf<ImageHeader> jpegHeader(std::string_view bytes) {
    jpeg_decompress_struct cinfo;
    JpegErrors errors;
    prepare(cinfo, errors);
    if (setjmp(errors.jump) != 0) {
        jpeg_destroy_decompress(&cinfo);
        return imageError(ImageErrorKind::Corrupt, std::string("broken JPEG header: ") + errors.message);
    }
    jpeg_create_decompress(&cinfo);
    setSource(cinfo, bytes);
    jpeg_read_header(&cinfo, TRUE);
    ImageHeader header;
    header.format = ImageFormat::Jpeg;
    header.size = {static_cast<int>(cinfo.image_width), static_cast<int>(cinfo.image_height)};
    header.progressive = cinfo.progressive_mode != 0;
    header.extraDecodeBytes = coefficientBytes(cinfo);
    jpeg_destroy_decompress(&cinfo);
    return header;
}

ImageResultOf<RgbaImage> decodeJpeg(std::string_view bytes, int denominator, std::uint64_t maxMemory,
                                    const net::CancelToken& cancel) {
    const auto image = std::make_unique<RgbaImage>();
    jpeg_decompress_struct cinfo;
    JpegErrors errors;
    prepare(cinfo, errors);
    if (setjmp(errors.jump) != 0) {
        jpeg_destroy_decompress(&cinfo);
        return imageError(ImageErrorKind::Corrupt, std::string("broken JPEG: ") + errors.message);
    }
    jpeg_create_decompress(&cinfo);
    // A second line of defence behind our own size check: libjpeg refuses to allocate its big buffers beyond this.
    cinfo.mem->max_memory_to_use = static_cast<long>(maxMemory < static_cast<std::uint64_t>(LONG_MAX) ? maxMemory : LONG_MAX);
    setSource(cinfo, bytes);
    jpeg_read_header(&cinfo, TRUE);
    const bool cmyk = isCmyk(cinfo);
    cinfo.out_color_space = cmyk ? JCS_CMYK : JCS_EXT_RGBA;
    cinfo.scale_num = 1;
    cinfo.scale_denom = static_cast<unsigned int>(denominator);
    jpeg_start_decompress(&cinfo);
    const std::size_t stride = static_cast<std::size_t>(cinfo.output_width) * 4;
    bool allocated = cinfo.output_components == 4;
    if (allocated) {
        try {
            image->pixels.resize(stride * cinfo.output_height);
        } catch (const std::bad_alloc&) {
            allocated = false;
        }
    }
    if (!allocated) {
        jpeg_destroy_decompress(&cinfo);
        return imageError(ImageErrorKind::TooLarge, "not enough memory to decode the JPEG");
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        if (cinfo.output_scanline % kCancelCheckRows == 0 && cancel.cancelled()) {
            jpeg_destroy_decompress(&cinfo);
            return imageError(ImageErrorKind::Cancelled, "cancelled");
        }
        JSAMPROW row = image->pixels.data() + static_cast<std::size_t>(cinfo.output_scanline) * stride;
        if (jpeg_read_scanlines(&cinfo, &row, 1) != 1) {
            break;  // can't happen with a memory source; finish_decompress then reports what's wrong
        }
    }
    jpeg_finish_decompress(&cinfo);
    image->size = {static_cast<int>(cinfo.output_width), static_cast<int>(cinfo.output_height)};
    const bool adobe = cinfo.saw_Adobe_marker != 0;
    const int warnings = errors.warnings;
    jpeg_destroy_decompress(&cinfo);
    if (warnings > 0) {
        return imageError(ImageErrorKind::Corrupt, std::string("broken JPEG: ") + errors.message);
    }
    if (cmyk) {
        cmykToRgba(image->pixels.data(), image->pixels.size() / 4, adobe);
    }
    return std::move(*image);
}

}  // namespace snx::image::detail
