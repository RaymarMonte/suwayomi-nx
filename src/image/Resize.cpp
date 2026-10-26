// The box filter behind shrinkImage(): each target pixel is the average of the source pixels it covers, each counted by the
// share of it that's covered. Done in two steps: a source row is first shrunk sideways (and kept, because the next target row
// usually starts on the same source row), then the rows that make up one target row are added up. Only two rows of floats
// are in use besides the two images, however tall the image.

#include <algorithm>
#include <cmath>
#include <new>

#include "image/Decode.hpp"
#include "image/Decoders.hpp"

namespace snx::image {

namespace {

// For one direction: target pixel t covers `count` source pixels starting at `first`, with these weights (they add up to 1).
struct Span {
    int first = 0;
    int count = 0;
    std::size_t weights = 0;  // where its weights start in AxisPlan::weights
};

struct AxisPlan {
    std::vector<Span> spans;
    std::vector<float> weights;
};

AxisPlan planAxis(int source, int target) {
    AxisPlan plan;
    plan.spans.reserve(static_cast<std::size_t>(target));
    const double scale = static_cast<double>(source) / target;
    for (int t = 0; t < target; ++t) {
        const double begin = t * scale;
        const double end = std::min((t + 1) * scale, static_cast<double>(source));
        Span span;
        span.first = std::clamp(static_cast<int>(std::floor(begin)), 0, source - 1);
        span.weights = plan.weights.size();
        const int last = std::clamp(static_cast<int>(std::ceil(end)), span.first + 1, source);
        double sum = 0;
        for (int s = span.first; s < last; ++s) {
            const double covered = std::min(end, s + 1.0) - std::max(begin, static_cast<double>(s));
            plan.weights.push_back(static_cast<float>(std::max(covered, 0.0)));
            sum += plan.weights.back();
        }
        span.count = last - span.first;
        for (int k = 0; k < span.count; ++k) {
            float& w = plan.weights[span.weights + static_cast<std::size_t>(k)];
            w = sum > 0 ? static_cast<float>(w / sum) : 1.0F / static_cast<float>(span.count);
        }
        plan.spans.push_back(span);
    }
    return plan;
}

// Rounds to the nearest byte. Plain arithmetic, not std::lround: that's a library call per channel per pixel, which the
// Switch benchmark showed is most of the time (WP-07). Sums are never negative.
std::uint8_t toByte(float value) {
    const int rounded = static_cast<int>(value + 0.5F);
    return static_cast<std::uint8_t>(rounded > 255 ? 255 : (rounded < 0 ? 0 : rounded));
}

}  // namespace

ImageResultOf<RgbaImage> shrinkImage(const RgbaImage& source, core::ImageSize size, const net::CancelToken& cancel) {
    if (source.size.width <= 0 || source.size.height <= 0 || source.pixels.size() != core::rgbaBytes(source.size) ||
        size.width <= 0 || size.height <= 0) {
        return imageError(ImageErrorKind::Corrupt, "nothing to shrink");
    }
    if (size == source.size) {
        return source;
    }
    RgbaImage out;
    out.size = size;
    out.hasAlpha = source.hasAlpha;
    std::vector<float> row;  // one source row, already shrunk sideways: R, G, B, A per target column
    std::vector<float> sum;  // the target row being added up
    try {
        out.pixels.resize(core::rgbaBytes(size));
        row.resize(static_cast<std::size_t>(size.width) * 4);
        sum.resize(row.size());
    } catch (const std::bad_alloc&) {
        return imageError(ImageErrorKind::TooLarge, "not enough memory to shrink the image");
    }
    const AxisPlan across = planAxis(source.size.width, size.width);
    const AxisPlan down = planAxis(source.size.height, size.height);
    const std::size_t sourceStride = static_cast<std::size_t>(source.size.width) * 4;
    const bool alpha = source.hasAlpha;

    // Colour is weighted by alpha (premultiplied) while adding up, so a see-through pixel adds no colour. Opaque images (most
    // pages) take the loop without that step.
    auto shrinkRow = [&](int sourceRow) {
        const std::uint8_t* pixels = source.pixels.data() + static_cast<std::size_t>(sourceRow) * sourceStride;
        for (int t = 0; t < size.width; ++t) {
            const Span& span = across.spans[static_cast<std::size_t>(t)];
            const std::uint8_t* p = pixels + static_cast<std::size_t>(span.first) * 4;
            const float* w = across.weights.data() + span.weights;
            float r = 0, g = 0, b = 0, a = 0;
            if (alpha) {
                for (int k = 0; k < span.count; ++k, p += 4) {
                    const float colourWeight = w[k] * p[3] / 255.0F;
                    r += p[0] * colourWeight;
                    g += p[1] * colourWeight;
                    b += p[2] * colourWeight;
                    a += p[3] * w[k];
                }
            } else {
                for (int k = 0; k < span.count; ++k, p += 4) {
                    r += p[0] * w[k];
                    g += p[1] * w[k];
                    b += p[2] * w[k];
                    a += p[3] * w[k];
                }
            }
            float* out4 = row.data() + static_cast<std::size_t>(t) * 4;
            out4[0] = r;
            out4[1] = g;
            out4[2] = b;
            out4[3] = a;
        }
    };

    int rowInBuffer = -1;
    for (int y = 0; y < size.height; ++y) {
        if (y % detail::kCancelCheckRows == 0 && cancel.cancelled()) {
            return imageError(ImageErrorKind::Cancelled, "cancelled");
        }
        std::fill(sum.begin(), sum.end(), 0.0F);
        const Span& span = down.spans[static_cast<std::size_t>(y)];
        for (int k = 0; k < span.count; ++k) {
            const int sourceRow = span.first + k;
            if (sourceRow != rowInBuffer) {
                shrinkRow(sourceRow);
                rowInBuffer = sourceRow;
            }
            const float w = down.weights[span.weights + static_cast<std::size_t>(k)];
            for (std::size_t i = 0; i < sum.size(); ++i) {
                sum[i] += row[i] * w;
            }
        }
        std::uint8_t* target = out.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) * 4;
        for (int x = 0; x < size.width; ++x) {
            const float* s = sum.data() + static_cast<std::size_t>(x) * 4;
            std::uint8_t* p = target + static_cast<std::size_t>(x) * 4;
            // With alpha, the sums are premultiplied: divide by the covered alpha (a / 255) to get the colour back.
            const float unpremultiply = alpha ? (s[3] > 0.0F ? 255.0F / s[3] : 0.0F) : 1.0F;
            p[0] = toByte(s[0] * unpremultiply);
            p[1] = toByte(s[1] * unpremultiply);
            p[2] = toByte(s[2] * unpremultiply);
            p[3] = toByte(s[3]);
        }
    }
    return out;
}

}  // namespace snx::image
