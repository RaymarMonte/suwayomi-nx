// GIF: the first frame only (WP-07), our own small decoder (D39: stb_image's GIF code has open memory-safety reports, and
// giflib isn't packaged for the Switch). A GIF is a header, a canvas size, colour tables, optional extension blocks and the
// frames; each frame's pixels are colour-table indexes compressed with LZW. Everything is read through bounds-checked readers,
// and the fuzz target (tests/fuzz/fuzz_image_decode.cpp) runs it under AddressSanitizer.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

#include "image/Decoders.hpp"

namespace snx::image::detail {

namespace {

constexpr int kMaxCodes = 4096;  // LZW codes are at most 12 bits
constexpr int kMaxCodeBits = 12;
constexpr std::size_t kCancelCheckPixels = 64 * 1024;

// Reads the file byte by byte. Reading past the end returns 0 and clears ok(), so the parser checks ok() once in a while
// instead of after every byte.
class ByteReader {
  public:
    explicit ByteReader(std::string_view bytes)
        : data_(reinterpret_cast<const std::uint8_t*>(bytes.data())), size_(bytes.size()) {}

    std::uint8_t byte() {
        if (pos_ >= size_) {
            ok_ = false;
            return 0;
        }
        return data_[pos_++];
    }
    int le16() {
        const int low = byte();
        return low | (byte() << 8);
    }
    void skip(std::size_t count) {
        if (count > size_ - pos_) {
            ok_ = false;
            pos_ = size_;
        } else {
            pos_ += count;
        }
    }
    // Data sub-blocks: a length byte, that many bytes, again, until a length of 0.
    void skipSubBlocks() {
        while (ok_) {
            const std::uint8_t length = byte();
            if (length == 0) {
                return;
            }
            skip(length);
        }
    }
    bool ok() const { return ok_; }

  private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

struct ColourTable {
    std::array<std::uint8_t, 256 * 3> rgb{};
    int count = 0;
};

void readColourTable(ByteReader& reader, int count, ColourTable& table) {
    table.count = count;
    for (int i = 0; i < count * 3; ++i) {
        table.rgb[static_cast<std::size_t>(i)] = reader.byte();
    }
}

// Everything before the first frame's pixel data.
struct FirstFrame {
    core::ImageSize canvas;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    bool interlaced = false;
    int transparent = -1;  // the colour index that means "see-through", or -1
    ColourTable colours;
};

// Reads up to the start of the first frame's LZW data. On failure, `error` says why.
bool readToFirstFrame(ByteReader& reader, FirstFrame& frame, std::string& error) {
    std::string signature;
    for (int i = 0; i < 6; ++i) {
        signature.push_back(static_cast<char>(reader.byte()));
    }
    if (!reader.ok() || (signature != "GIF87a" && signature != "GIF89a")) {
        error = "not a GIF";
        return false;
    }
    const int screenWidth = reader.le16();
    const int screenHeight = reader.le16();
    const std::uint8_t screenFlags = reader.byte();
    reader.skip(2);  // background colour, aspect ratio
    ColourTable global;
    if ((screenFlags & 0x80) != 0) {
        readColourTable(reader, 2 << (screenFlags & 7), global);
    }
    int transparent = -1;
    while (reader.ok()) {
        const std::uint8_t block = reader.byte();
        if (block == 0x21) {  // an extension
            const std::uint8_t label = reader.byte();
            if (label == 0xF9) {  // graphics control: may name the transparent colour of the next frame
                const std::uint8_t length = reader.byte();
                if (length >= 4) {
                    const std::uint8_t flags = reader.byte();
                    reader.skip(2);  // delay
                    const std::uint8_t index = reader.byte();
                    transparent = (flags & 1) != 0 ? index : -1;
                    reader.skip(length - 4U);
                } else {
                    reader.skip(length);
                }
            }
            reader.skipSubBlocks();
        } else if (block == 0x2C) {  // the first frame
            frame.left = reader.le16();
            frame.top = reader.le16();
            frame.width = reader.le16();
            frame.height = reader.le16();
            const std::uint8_t flags = reader.byte();
            frame.interlaced = (flags & 0x40) != 0;
            if ((flags & 0x80) != 0) {
                readColourTable(reader, 2 << (flags & 7), frame.colours);
            } else {
                frame.colours = global;
            }
            if (!reader.ok()) {
                break;
            }
            if (frame.width == 0 || frame.height == 0 || frame.colours.count == 0) {
                error = frame.colours.count == 0 ? "GIF frame without colours" : "empty GIF frame";
                return false;
            }
            frame.transparent = transparent;
            // Some encoders write a canvas smaller than the frame (even 0×0): make room for the whole frame.
            frame.canvas = {std::max(screenWidth, frame.left + frame.width), std::max(screenHeight, frame.top + frame.height)};
            return true;
        } else if (block == 0x3B) {  // the end, before any frame
            error = "GIF without a frame";
            return false;
        } else {
            error = "broken GIF block";
            return false;
        }
    }
    error = "GIF cut short";
    return false;
}

// LZW codes come in data sub-blocks, packed lowest bit first.
class CodeReader {
  public:
    explicit CodeReader(ByteReader& reader) : reader_(reader) {}

    // The next code of `bits` bits, or -1 at the end of the data.
    int next(int bits) {
        while (count_ < bits) {
            if (blockLeft_ == 0) {
                blockLeft_ = ended_ ? 0 : reader_.byte();
                if (blockLeft_ == 0 || !reader_.ok()) {
                    ended_ = true;
                    return -1;
                }
            }
            buffer_ |= static_cast<std::uint32_t>(reader_.byte()) << count_;
            count_ += 8;
            --blockLeft_;
            if (!reader_.ok()) {
                ended_ = true;
                return -1;
            }
        }
        const int code = static_cast<int>(buffer_ & ((1U << bits) - 1));
        buffer_ >>= bits;
        count_ -= bits;
        return code;
    }

  private:
    ByteReader& reader_;
    std::uint32_t buffer_ = 0;
    int count_ = 0;
    int blockLeft_ = 0;
    bool ended_ = false;
};

// The LZW string table: each code is a shorter code (prefix) plus one more index (suffix).
struct LzwTable {
    std::array<std::uint16_t, kMaxCodes> prefix{};
    std::array<std::uint8_t, kMaxCodes> suffix{};
    std::array<std::uint8_t, kMaxCodes> first{};       // the first index of each code's string
    std::array<std::uint8_t, kMaxCodes + 1> stack{};   // one decoded string, last index first
};

// Row `pass row` of an interlaced frame: rows 0, 8, 16, ... first, then 4, 12, ..., then 2, 6, ..., then 1, 3, ...
int interlacedRow(int row, int height) {
    const int pass1 = (height + 7) / 8;
    if (row < pass1) {
        return row * 8;
    }
    row -= pass1;
    const int pass2 = (height + 3) / 8;
    if (row < pass2) {
        return 4 + row * 8;
    }
    row -= pass2;
    const int pass3 = (height + 1) / 4;
    if (row < pass3) {
        return 2 + row * 4;
    }
    row -= pass3;
    return 1 + row * 2;
}

}  // namespace

ImageResultOf<ImageHeader> gifHeader(std::string_view bytes) {
    ByteReader reader(bytes);
    FirstFrame frame;
    std::string error;
    if (!readToFirstFrame(reader, frame, error)) {
        return imageError(ImageErrorKind::Corrupt, error);
    }
    ImageHeader header;
    header.format = ImageFormat::Gif;
    header.size = frame.canvas;
    header.hasAlpha = frame.transparent >= 0 || frame.width != frame.canvas.width || frame.height != frame.canvas.height;
    return header;
}

ImageResultOf<RgbaImage> decodeGif(std::string_view bytes, const net::CancelToken& cancel) {
    ByteReader reader(bytes);
    FirstFrame frame;
    std::string error;
    if (!readToFirstFrame(reader, frame, error)) {
        return imageError(ImageErrorKind::Corrupt, error);
    }
    const int minCodeBits = reader.byte();
    if (!reader.ok() || minCodeBits < 1 || minCodeBits >= kMaxCodeBits) {
        return imageError(ImageErrorKind::Corrupt, "broken GIF data");
    }
    RgbaImage out;
    out.size = frame.canvas;
    out.hasAlpha = frame.transparent >= 0 || frame.width != frame.canvas.width || frame.height != frame.canvas.height;
    std::unique_ptr<LzwTable> table;
    try {
        out.pixels.assign(static_cast<std::size_t>(frame.canvas.width) * static_cast<std::size_t>(frame.canvas.height) * 4,
                          0);  // outside the frame: transparent
        table = std::make_unique<LzwTable>();
    } catch (const std::bad_alloc&) {
        return imageError(ImageErrorKind::TooLarge, "not enough memory to decode the GIF");
    }

    const int clearCode = 1 << minCodeBits;
    const int endCode = clearCode + 1;
    for (int i = 0; i < clearCode; ++i) {
        table->suffix[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        table->first[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    }
    int codeBits = minCodeBits + 1;
    int nextCode = clearCode + 2;
    int previous = -1;

    const std::size_t total = static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    std::size_t written = 0;
    std::size_t nextCancelCheck = kCancelCheckPixels;
    int x = 0;
    int row = 0;
    int y = 0;  // the frame row that `row` lands on (interlacing)
    const std::size_t canvasStride = static_cast<std::size_t>(frame.canvas.width) * 4;
    CodeReader codes(reader);

    while (written < total) {
        if (written >= nextCancelCheck) {
            if (cancel.cancelled()) {
                return imageError(ImageErrorKind::Cancelled, "cancelled");
            }
            nextCancelCheck += kCancelCheckPixels;
        }
        const int code = codes.next(codeBits);
        if (code < 0 || code == endCode) {
            break;
        }
        if (code == clearCode) {
            codeBits = minCodeBits + 1;
            nextCode = clearCode + 2;
            previous = -1;
            continue;
        }
        int top = 0;
        int emit = code;  // the code whose string comes out now
        if (previous < 0) {
            if (code > clearCode) {
                return imageError(ImageErrorKind::Corrupt, "broken GIF data");
            }
        } else if (code == nextCode) {
            // A code that's being defined right now: the previous string plus its own first index.
            table->stack[static_cast<std::size_t>(top++)] = table->first[static_cast<std::size_t>(previous)];
            emit = previous;
        } else if (code > nextCode) {
            return imageError(ImageErrorKind::Corrupt, "broken GIF data");
        }
        // Walk back from `emit` to a single index. Every prefix is a smaller code, so this ends within kMaxCodes steps.
        for (int c = emit;; c = table->prefix[static_cast<std::size_t>(c)]) {
            table->stack[static_cast<std::size_t>(top++)] = table->suffix[static_cast<std::size_t>(c)];
            if (c < clearCode) {
                break;
            }
        }
        if (previous >= 0 && nextCode < kMaxCodes) {
            table->prefix[static_cast<std::size_t>(nextCode)] = static_cast<std::uint16_t>(previous);
            table->suffix[static_cast<std::size_t>(nextCode)] = table->first[static_cast<std::size_t>(emit)];
            table->first[static_cast<std::size_t>(nextCode)] = table->first[static_cast<std::size_t>(previous)];
            ++nextCode;
            if (nextCode == (1 << codeBits) && codeBits < kMaxCodeBits) {
                ++codeBits;
            }
        }
        previous = code;

        while (top > 0 && written < total) {
            const int index = table->stack[static_cast<std::size_t>(--top)];
            if (index != frame.transparent) {
                std::uint8_t* p = out.pixels.data() + static_cast<std::size_t>(frame.top + y) * canvasStride +
                                  static_cast<std::size_t>(frame.left + x) * 4;
                if (index < frame.colours.count) {
                    p[0] = frame.colours.rgb[static_cast<std::size_t>(index) * 3];
                    p[1] = frame.colours.rgb[static_cast<std::size_t>(index) * 3 + 1];
                    p[2] = frame.colours.rgb[static_cast<std::size_t>(index) * 3 + 2];
                }
                p[3] = 255;  // an index past the colour table shows black, like other viewers
            }
            ++written;
            if (++x == frame.width) {
                x = 0;
                ++row;
                y = frame.interlaced ? interlacedRow(row, frame.height) : row;
                if (y >= frame.height) {
                    y = frame.height - 1;  // only after the last pixel; never written to
                }
            }
        }
    }
    if (written < total) {
        return imageError(ImageErrorKind::Corrupt, "GIF data cut short");
    }
    return out;
}

}  // namespace snx::image::detail
