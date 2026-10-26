// image: sniffing, the decoders, the size guard (SAFETY.md S6), shrinking and tiles (WP-07). Fixture images: tests/fixtures/images/.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "Fixtures.hpp"
#include "core/ImageGeometry.hpp"
#include "image/Caches.hpp"
#include "image/Decode.hpp"
#include "image/DecodedImage.hpp"
#include "image/WorkerPool.hpp"

using namespace snx;
using image::ImageErrorKind;
using image::ImageFormat;

namespace {

image::DecodeLimits limits(std::uint64_t maxDecodeBytes = 128 * core::kMiB) {
    return image::DecodeLimits{16384, maxDecodeBytes};
}

std::string fixtureImage(const std::string& name) {
    return readFixture("images/" + name);
}

image::ImageResultOf<image::RgbaImage> decode(const std::string& bytes, core::ImageSize box = {0, 0},
                                              std::uint64_t maxDecodeBytes = 128 * core::kMiB,
                                              image::DecodeInfo* info = nullptr) {
    return image::decodeImage(bytes, box, limits(maxDecodeBytes), net::CancelToken(), info);
}

const std::uint8_t* pixelAt(const image::RgbaImage& img, int x, int y) {
    return img.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.size.width) + x) * 4;
}

// A PNG header (signature + IHDR) that claims any size, without pixel data: enough for the size guard.
std::string pngHeaderClaiming(std::uint32_t width, std::uint32_t height) {
    std::string png("\x89PNG\r\n\x1A\n", 8);
    const unsigned char ihdr[] = {0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                  static_cast<unsigned char>(width >> 24), static_cast<unsigned char>(width >> 16),
                                  static_cast<unsigned char>(width >> 8), static_cast<unsigned char>(width),
                                  static_cast<unsigned char>(height >> 24), static_cast<unsigned char>(height >> 16),
                                  static_cast<unsigned char>(height >> 8), static_cast<unsigned char>(height),
                                  8, 6, 0, 0, 0};
    png.append(reinterpret_cast<const char*>(ihdr), sizeof ihdr);
    // libpng checks the IHDR's CRC: compute it (CRC-32 over type + data).
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 4; i < sizeof ihdr; ++i) {
        crc ^= ihdr[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    crc ^= 0xFFFFFFFFu;
    for (int shift = 24; shift >= 0; shift -= 8) {
        png.push_back(static_cast<char>((crc >> shift) & 0xFF));
    }
    png.append(std::string("\0\0\0\0IDAT", 8));  // libpng reads the header up to the first IDAT chunk's start
    return png;
}

// A GIF whose canvas claims any size, with a 1x1 first frame.
std::string gifClaiming(int width, int height) {
    std::string gif = "GIF89a";
    gif += static_cast<char>(width & 0xFF);
    gif += static_cast<char>(width >> 8);
    gif += static_cast<char>(height & 0xFF);
    gif += static_cast<char>(height >> 8);
    gif += std::string("\x80\x00\x00", 3);         // a 2-colour global table
    gif += std::string("\x00\x00\x00\xFF\xFF\xFF", 6);
    gif += std::string("\x2C\x00\x00\x00\x00\x01\x00\x01\x00\x00", 10);  // a 1x1 frame
    gif += std::string("\x02\x02\x44\x01\x00\x3B", 6);                      // LZW: clear, 0, end; trailer
    return gif;
}

}  // namespace

TEST_SUITE("image") {
    TEST_CASE("sniffFormat: magic bytes, not names") {
        CHECK(image::sniffFormat(fixtureImage("alpha-p1.jpg")) == ImageFormat::Jpeg);
        CHECK(image::sniffFormat(fixtureImage("beta-p1.png")) == ImageFormat::Png);
        CHECK(image::sniffFormat(fixtureImage("gamma-strip.webp")) == ImageFormat::Webp);
        CHECK(image::sniffFormat(fixtureImage("small.gif")) == ImageFormat::Gif);
        CHECK(image::sniffFormat("") == ImageFormat::Unknown);
        CHECK(image::sniffFormat("BM\x00\x00") == ImageFormat::Unknown);
        CHECK(image::sniffFormat("RIFF\x10\x00\x00\x00WAVE") == ImageFormat::Unknown);
        CHECK(std::string(image::imageFormatName(ImageFormat::Webp)) == "webp");
        const auto bmp = image::readHeader(std::string("BM6\x00\x00\x00", 6));
        REQUIRE_FALSE(bmp.ok());
        CHECK(bmp.error().kind == ImageErrorKind::Unsupported);
    }

    TEST_CASE("every format decodes, at the planned size") {
        struct Case {
            const char* file;
            ImageFormat format;
            core::ImageSize box, expected, source;
        };
        const Case cases[] = {
            {"alpha-p1.jpg", ImageFormat::Jpeg, {1280, 720}, {480, 720}, {1000, 1500}},
            {"alpha-progressive.jpg", ImageFormat::Jpeg, {0, 0}, {1000, 1500}, {1000, 1500}},
            {"alpha-cmyk.jpg", ImageFormat::Jpeg, {0, 0}, {200, 300}, {200, 300}},
            {"beta-p1.png", ImageFormat::Png, {1280, 720}, {480, 720}, {1000, 1500}},
            {"beta-spread.png", ImageFormat::Png, {1280, 720}, {960, 720}, {2000, 1500}},
            {"grey16.png", ImageFormat::Png, {0, 0}, {40, 30}, {40, 30}},
            {"gamma-strip.webp", ImageFormat::Webp, {2560, 0}, {800, 12000}, {800, 12000}},
            {"gamma-strip.webp", ImageFormat::Webp, {400, 0}, {400, 6000}, {800, 12000}},
            {"lossless.webp", ImageFormat::Webp, {0, 0}, {300, 200}, {300, 200}},
            {"small.gif", ImageFormat::Gif, {50, 0}, {50, 75}, {100, 150}},
            {"broken-tiny.jpg", ImageFormat::Jpeg, {1280, 720}, {1, 1}, {1, 1}},
        };
        for (const Case& c : cases) {
            CAPTURE(c.file);
            CAPTURE(c.box.width);
            image::DecodeInfo info;
            const auto result = decode(fixtureImage(c.file), c.box, 128 * core::kMiB, &info);
            REQUIRE_MESSAGE(result.ok(), (result.ok() ? "" : result.error().message));
            CHECK(result.value().size == c.expected);
            CHECK(result.value().pixels.size() == core::rgbaBytes(c.expected));
            CHECK(info.header.format == c.format);
            CHECK(info.header.size == c.source);
            CHECK(info.plan.workBytes <= 128 * core::kMiB);
        }
    }

    TEST_CASE("decoded pixels are right: colours, CMYK, alpha, GIF frames") {
        // The fixture page's background is light; its top-left corner is plain background.
        const auto jpeg = decode(fixtureImage("alpha-p1.jpg"));
        REQUIRE(jpeg.ok());
        const std::uint8_t* corner = pixelAt(jpeg.value(), 2, 2);
        CHECK(corner[3] == 255);
        CHECK_FALSE(jpeg.value().hasAlpha);
        const auto progressive = decode(fixtureImage("alpha-progressive.jpg"));
        REQUIRE(progressive.ok());
        const std::uint8_t* progressiveCorner = pixelAt(progressive.value(), 2, 2);
        for (int ch = 0; ch < 3; ++ch) {
            CHECK(std::abs(int(corner[ch]) - int(progressiveCorner[ch])) < 12);  // same picture, other encoding
        }
        // ImageMagick's RGB -> CMYK step is lossy; reading the same file, it gets srgb(90, 95, 93) at (1, 1). So do we.
        const auto cmyk = decode(fixtureImage("alpha-cmyk.jpg"));
        REQUIRE(cmyk.ok());
        const std::uint8_t* cmykCorner = pixelAt(cmyk.value(), 1, 1);
        const int expected[] = {90, 95, 93};
        for (int ch = 0; ch < 3; ++ch) {
            CHECK(std::abs(int(cmykCorner[ch]) - expected[ch]) < 8);
        }

        const auto transparent = decode(fixtureImage("transparent.gif"));
        REQUIRE(transparent.ok());
        CHECK(transparent.value().hasAlpha);
        CHECK(pixelAt(transparent.value(), 0, 0)[3] == 0);    // outside the rectangle
        CHECK(pixelAt(transparent.value(), 20, 20)[0] > 200);  // inside: red
        CHECK(pixelAt(transparent.value(), 20, 20)[3] == 255);

        const auto animated = decode(fixtureImage("animated.gif"));
        REQUIRE(animated.ok());
        CHECK(pixelAt(animated.value(), 16, 16)[0] > 200);  // the first frame is red, the second blue
        CHECK(pixelAt(animated.value(), 16, 16)[2] < 50);

        const auto plain = decode(fixtureImage("small.gif"));
        const auto interlaced = decode(fixtureImage("small-interlaced.gif"));
        REQUIRE(plain.ok());
        REQUIRE(interlaced.ok());
        CHECK(plain.value().pixels == interlaced.value().pixels);  // same picture, rows stored in another order

        const auto lossless = decode(fixtureImage("lossless.webp"));
        REQUIRE(lossless.ok());
        CHECK(lossless.value().hasAlpha);
        CHECK(std::abs(int(pixelAt(lossless.value(), 0, 0)[3]) - 128) < 3);

        const auto grey = decode(fixtureImage("grey16.png"));
        REQUIRE(grey.ok());
        CHECK(grey.value().hasAlpha);
        CHECK(std::abs(int(pixelAt(grey.value(), 0, 0)[3]) - 191) < 3);
    }

    TEST_CASE("broken files are errors, never crashes") {
        const auto corrupt = decode(fixtureImage("broken-corrupt.jpg"));
        REQUIRE_FALSE(corrupt.ok());
        CHECK(corrupt.error().kind == ImageErrorKind::Corrupt);
        const auto empty = decode(fixtureImage("broken-empty.jpg"));
        REQUIRE_FALSE(empty.ok());
        CHECK(empty.error().kind == ImageErrorKind::Corrupt);
        // Cut short anywhere, every format fails cleanly.
        for (const char* name : {"alpha-p1.jpg", "beta-p1.png", "gamma-strip.webp", "small.gif", "lossless.webp"}) {
            CAPTURE(name);
            const std::string bytes = fixtureImage(name);
            for (const std::size_t keep : {std::size_t(4), std::size_t(40), bytes.size() / 2}) {
                CAPTURE(keep);
                const auto cut = decode(bytes.substr(0, keep));
                CHECK_FALSE(cut.ok());
            }
        }
        CHECK(decode("GIF89a").error().kind == ImageErrorKind::Corrupt);
        CHECK(decode("not an image at all").error().kind == ImageErrorKind::Unsupported);
    }

    TEST_CASE("the size guard refuses huge images from the header alone (S6)") {
        // Each of these claims a huge size but is only a few bytes: refused before anything big is allocated.
        const auto png = decode(pngHeaderClaiming(20000, 20000));
        REQUIRE_FALSE(png.ok());
        CHECK(png.error().kind == ImageErrorKind::TooLarge);
        const auto wide = decode(pngHeaderClaiming(16385, 10));
        REQUIRE_FALSE(wide.ok());
        CHECK(wide.error().kind == ImageErrorKind::TooLarge);
        const auto gif = decode(gifClaiming(30000, 30000));
        REQUIRE_FALSE(gif.ok());
        CHECK(gif.error().kind == ImageErrorKind::TooLarge);
        CHECK(decode(gifClaiming(3, 2)).ok());  // the same file with a small canvas is fine
        // A PNG within the sides but over the decode budget at full size.
        const auto budget = decode(pngHeaderClaiming(8000, 8000), {100, 100}, 64 * core::kMiB);
        REQUIRE_FALSE(budget.ok());
        CHECK(budget.error().kind == ImageErrorKind::TooLarge);
        // A JPEG's SOF with a 16385-pixel side.
        std::string jpeg = fixtureImage("alpha-p1.jpg");
        const std::size_t sof = jpeg.find("\xFF\xC0", 2);
        REQUIRE(sof != std::string::npos);
        jpeg[sof + 7] = static_cast<char>(0x40);  // width 0x4001
        jpeg[sof + 8] = static_cast<char>(0x01);
        const auto huge = decode(jpeg);
        REQUIRE_FALSE(huge.ok());
        CHECK(huge.error().kind == ImageErrorKind::TooLarge);
    }

    TEST_CASE("no single decode needs more than maxDecodeBytes: the webtoon strip and a progressive JPEG") {
        // 800x12000 is 36.6 MiB at full size. With a 16 MiB budget it still shows, smaller.
        image::DecodeInfo info;
        const auto strip = decode(fixtureImage("gamma-strip.webp"), {0, 0}, 16 * core::kMiB, &info);
        REQUIRE(strip.ok());
        CHECK(core::rgbaBytes(strip.value().size) <= 16 * core::kMiB);
        CHECK(info.plan.workBytes <= 16 * core::kMiB);
        // A progressive JPEG keeps its coefficients (here ~4.5 MiB); with 6 MiB in all it decodes at a smaller scale.
        image::DecodeInfo progressive;
        const auto small = decode(fixtureImage("alpha-progressive.jpg"), {0, 0}, 6 * core::kMiB, &progressive);
        REQUIRE(small.ok());
        CHECK(progressive.header.progressive);
        CHECK(progressive.header.extraDecodeBytes > 4 * core::kMiB);
        CHECK(progressive.plan.denominator >= 2);
        CHECK(progressive.plan.workBytes <= 6 * core::kMiB);
        const auto none = decode(fixtureImage("alpha-progressive.jpg"), {0, 0}, 4 * core::kMiB);
        REQUIRE_FALSE(none.ok());
        CHECK(none.error().kind == ImageErrorKind::TooLarge);
        // The probe is asked while both buffers are alive.
        image::DecodeInfo probed;
        int asked = 0;
        REQUIRE(image::decodeImage(fixtureImage("alpha-p1.jpg"), {300, 300}, limits(), net::CancelToken(), &probed,
                                   [&] {
                                       ++asked;
                                       return std::uint64_t{12345};
                                   })
                    .ok());
        CHECK(asked == 1);
        CHECK(probed.memUsedAtPeak == 12345);
    }

    TEST_CASE("shrinkImage is a box filter: averages, alpha-weighted") {
        image::RgbaImage checker;
        checker.size = {4, 4};
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const std::uint8_t v = ((x + y) % 2 == 0) ? 255 : 0;
                checker.pixels.insert(checker.pixels.end(), {v, v, v, 255});
            }
        }
        const auto half = image::shrinkImage(checker, {2, 2}, net::CancelToken());
        REQUIRE(half.ok());
        for (int i = 0; i < 4; ++i) {
            CHECK(half.value().pixels[static_cast<std::size_t>(i) * 4] == 128);  // black and white average to grey
        }
        // Opaque red next to see-through black: the colour stays red (no dark fringe), alpha halves.
        image::RgbaImage edge;
        edge.size = {2, 1};
        edge.hasAlpha = true;
        edge.pixels = {255, 0, 0, 255, 0, 0, 0, 0};
        const auto one = image::shrinkImage(edge, {1, 1}, net::CancelToken());
        REQUIRE(one.ok());
        CHECK(one.value().pixels[0] == 255);
        CHECK(one.value().pixels[1] == 0);
        CHECK(one.value().pixels[3] == 128);
        // A 3:2 shrink weighs the middle pixel half to each side.
        image::RgbaImage row;
        row.size = {3, 1};
        row.pixels = {0, 0, 0, 255, 90, 90, 90, 255, 180, 180, 180, 255};
        const auto two = image::shrinkImage(row, {2, 1}, net::CancelToken());
        REQUIRE(two.ok());
        CHECK(two.value().pixels[0] == 30);   // (0 * 1 + 90 * 0.5) / 1.5
        CHECK(two.value().pixels[4] == 150);  // (90 * 0.5 + 180 * 1) / 1.5
        CHECK_FALSE(image::shrinkImage(image::RgbaImage{}, {1, 1}, net::CancelToken()).ok());
    }

    TEST_CASE("cancel: before a decode, and in the middle of a long shrink") {
        net::CancelToken cancelled;
        cancelled.cancel();
        for (const char* name : {"alpha-p1.jpg", "beta-p1.png", "gamma-strip.webp", "small.gif"}) {
            CAPTURE(name);
            const auto result = image::decodeImage(fixtureImage(name), {100, 0}, limits(), cancelled);
            REQUIRE_FALSE(result.ok());
            CHECK(result.error().kind == ImageErrorKind::Cancelled);
        }
        // A big shrink (3000x3000 -> 2999x2999 takes a good while) stops soon after its token fires on another thread.
        image::RgbaImage big;
        big.size = {3000, 3000};
        big.pixels.assign(core::rgbaBytes(big.size), 200);
        net::CancelToken token;
        std::thread canceller([token] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            token.cancel();
        });
        const auto started = std::chrono::steady_clock::now();
        const auto shrunk = image::shrinkImage(big, {2999, 2999}, token);
        const auto took = std::chrono::steady_clock::now() - started;
        canceller.join();
        REQUIRE_FALSE(shrunk.ok());
        CHECK(shrunk.error().kind == ImageErrorKind::Cancelled);
        CHECK(took < std::chrono::seconds(5));
    }

    TEST_CASE("DecodedImage: tiles, bytes, tile pixels") {
        auto strip = decode(fixtureImage("gamma-strip.webp"), {0, 0});
        REQUIRE(strip.ok());
        const image::DecodedImage tall(std::move(strip.value()), 2048, ImageFormat::Webp, {800, 12000});
        REQUIRE(tall.tiles().size() == 6);
        CHECK(tall.bytes() >= 800ULL * 12000 * 4);
        std::vector<std::uint8_t> scratch;
        // A tile as wide as the image is a pointer into it, no copy.
        CHECK(tall.tilePixels(1, scratch) == tall.pixels() + 2000ULL * 800 * 4);
        CHECK(scratch.empty());
        CHECK(tall.tilePixels(6, scratch) == nullptr);

        image::RgbaImage wide;
        wide.size = {5000, 2};
        wide.pixels.resize(core::rgbaBytes(wide.size));
        for (std::size_t i = 0; i < wide.pixels.size(); ++i) {
            wide.pixels[i] = static_cast<std::uint8_t>(i % 251);
        }
        const std::vector<std::uint8_t> original = wide.pixels;
        const image::DecodedImage grid(std::move(wide), 2048, ImageFormat::Png, {5000, 2});
        REQUIRE(grid.tiles().size() == 3);
        const core::TileRect t = grid.tiles()[1];
        const std::uint8_t* copied = grid.tilePixels(1, scratch);
        REQUIRE(copied == scratch.data());
        CHECK(scratch.size() == static_cast<std::size_t>(t.width) * t.height * 4);
        CHECK(copied[0] == original[static_cast<std::size_t>(t.x) * 4]);                       // row 0
        CHECK(copied[t.width * 4] == original[(5000 + static_cast<std::size_t>(t.x)) * 4]);  // row 1
    }

    TEST_CASE("TextureBudget and TextureLease (S7)") {
        image::TextureBudget budget(100);
        CHECK(budget.tryReserve(60));
        CHECK_FALSE(budget.tryReserve(50));
        {
            image::TextureLease lease(budget, 40);
            CHECK(lease.ok());
            CHECK(budget.used() == 100);
            image::TextureLease refused(budget, 1);
            CHECK_FALSE(refused.ok());
            image::TextureLease moved = std::move(lease);
            CHECK(moved.ok());
            CHECK_FALSE(lease.ok());  // NOLINT(bugprone-use-after-move): moved-from is empty on purpose
        }
        CHECK(budget.used() == 60);
        budget.release(1000);
        CHECK(budget.used() == 0);
        CHECK(image::TextureBudget::bytesFor({0, 0, 2048, 2000}) == 2048ULL * 2000 * 4);
    }

    TEST_CASE("WorkerPool: priority order, delays, exceptions stay inside, stop drops the rest") {
        image::WorkerPool pool(1, 256 * 1024);
        REQUIRE(pool.threads() == 1);
        std::atomic<int> errors{0};
        pool.setOnException([&](const char*) { ++errors; });
        std::vector<int> order;
        std::mutex mutex;
        std::atomic<bool> gate{false};
        pool.post(0, [&] {
            while (!gate.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        pool.post(3, [&] { std::lock_guard<std::mutex> lock(mutex); order.push_back(3); });
        pool.post(1, [&] { std::lock_guard<std::mutex> lock(mutex); order.push_back(1); });
        pool.post(1, [&] { std::lock_guard<std::mutex> lock(mutex); order.push_back(11); });
        pool.post(2, [&] { throw std::runtime_error("boom"); });
        std::atomic<bool> delayed{false};
        const auto posted = std::chrono::steady_clock::now();
        std::atomic<long long> delayedAfterMs{0};
        pool.postAfter(30, 0, [&] {
            delayedAfterMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - posted)
                                 .count();
            delayed = true;
        });
        gate = true;
        for (int i = 0; i < 500 && !delayed.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        REQUIRE(delayed.load());
        CHECK(delayedAfterMs.load() >= 30);
        {
            std::lock_guard<std::mutex> lock(mutex);
            CHECK(order == std::vector<int>{1, 11, 3});
        }
        CHECK(errors.load() == 1);
        std::atomic<bool> ranAfterStop{false};
        pool.postAfter(10000, 0, [&] { ranAfterStop = true; });
        pool.stop();
        pool.post(0, [&] { ranAfterStop = true; });
        CHECK(pool.waiting() == 0);
        CHECK_FALSE(ranAfterStop.load());
    }
}
