#include "core/ImageGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace snx::core {

namespace {

bool empty(ImageSize size) {
    return size.width <= 0 || size.height <= 0;
}

int ceilDiv(int value, int divisor) {
    return (value + divisor - 1) / divisor;
}

// The image's tiles grouped into rows: the index range of each row and where it is.
struct TileRow {
    int first = 0;
    int last = 0;  // one past
    int top = 0;
    int bottom = 0;  // one past
};

std::vector<TileRow> rowsOf(const std::vector<TileRect>& tiles) {
    std::vector<TileRow> rows;
    for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
        const TileRect& tile = tiles[static_cast<std::size_t>(i)];
        if (rows.empty() || tile.y != rows.back().top) {
            rows.push_back(TileRow{i, i + 1, tile.y, tile.y + tile.height});
        } else {
            rows.back().last = i + 1;
            rows.back().bottom = std::max(rows.back().bottom, tile.y + tile.height);
        }
    }
    return rows;
}

}  // namespace

std::uint64_t rgbaBytes(ImageSize size) {
    if (empty(size)) {
        return 0;
    }
    return static_cast<std::uint64_t>(size.width) * static_cast<std::uint64_t>(size.height) * 4U;
}

ImageSize fitInside(ImageSize image, ImageSize box) {
    if (empty(image)) {
        return {0, 0};
    }
    double scale = 1.0;
    if (box.width > 0 && image.width > box.width) {
        scale = std::min(scale, static_cast<double>(box.width) / image.width);
    }
    if (box.height > 0 && image.height > box.height) {
        scale = std::min(scale, static_cast<double>(box.height) / image.height);
    }
    if (scale >= 1.0) {
        return image;
    }
    ImageSize out{static_cast<int>(std::lround(image.width * scale)), static_cast<int>(std::lround(image.height * scale))};
    // Rounding must not push a side over the box, nor down to nothing.
    if (box.width > 0) {
        out.width = std::min(out.width, box.width);
    }
    if (box.height > 0) {
        out.height = std::min(out.height, box.height);
    }
    out.width = std::max(out.width, 1);
    out.height = std::max(out.height, 1);
    return out;
}

ImageSize fitDecoded(ImageSize image, ImageSize box, std::uint64_t maxBytes) {
    ImageSize out = fitInside(image, box);
    // Each step shrinks both sides by the square root of how much too big it is; rounding down can leave it a hair over,
    // so repeat (twice at most in practice).
    while (rgbaBytes(out) > maxBytes && (out.width > 1 || out.height > 1)) {
        const double factor = std::sqrt(static_cast<double>(maxBytes) / static_cast<double>(rgbaBytes(out)));
        const ImageSize smaller{std::max(1, static_cast<int>(out.width * factor)),
                                std::max(1, static_cast<int>(out.height * factor))};
        if (smaller == out) {
            // Too small a step to change anything: take one pixel off the longer side.
            (out.width >= out.height ? out.width : out.height) -= 1;
        } else {
            out = smaller;
        }
    }
    return out;
}

ImageSize scaledBy(ImageSize image, int denominator) {
    if (empty(image) || denominator < 1) {
        return {0, 0};
    }
    return {ceilDiv(image.width, denominator), ceilDiv(image.height, denominator)};
}

namespace {

// Whether `decoded` is at most slackPercent bigger than `output` on both sides.
bool withinSlack(ImageSize decoded, ImageSize output, int slackPercent) {
    const std::int64_t factor = 100 + std::max(slackPercent, 0);
    return static_cast<std::int64_t>(decoded.width) * 100 <= static_cast<std::int64_t>(output.width) * factor &&
           static_cast<std::int64_t>(decoded.height) * 100 <= static_cast<std::int64_t>(output.height) * factor;
}

// The decoder makes `decoded`; shrink it to fit `target` with what is left of the budget (or keep it, if it's within the
// slack). Fails if not even 1×1 fits.
DecodePlan shrinkWithin(ImageSize decoded, ImageSize target, std::uint64_t budget, std::uint64_t extraBytes,
                        int slackPercent) {
    DecodePlan plan;
    const std::uint64_t decodedBytes = rgbaBytes(decoded);
    if (decodedBytes > budget) {
        return plan;
    }
    plan.decoded = decoded;
    plan.output = fitInside(decoded, target);
    if (withinSlack(decoded, plan.output, slackPercent)) {
        plan.output = decoded;
    }
    if (plan.output != decoded) {
        const std::uint64_t room = budget - decodedBytes;
        if (room < rgbaBytes({1, 1})) {
            return plan;
        }
        plan.output = fitDecoded(decoded, target, room);
    }
    plan.ok = true;
    plan.workBytes = extraBytes + decodedBytes + (plan.output != decoded ? rgbaBytes(plan.output) : 0);
    return plan;
}

}  // namespace

DecodePlan planDecode(ImageSize image, ImageSize box, DecodeScaling scaling, std::uint64_t maxBytes,
                      std::uint64_t extraBytes, int slackPercent) {
    if (empty(image) || extraBytes >= maxBytes) {
        return {};
    }
    const std::uint64_t budget = maxBytes - extraBytes;
    const ImageSize target = fitInside(image, box);
    switch (scaling) {
        case DecodeScaling::None:
            return shrinkWithin(image, target, budget, extraBytes, slackPercent);
        case DecodeScaling::Any: {
            // The decoder scales for free, so no slack is needed: it decodes straight to the target.
            DecodePlan plan;
            plan.decoded = plan.output = fitDecoded(image, target, budget);
            plan.ok = rgbaBytes(plan.output) <= budget;
            plan.workBytes = extraBytes + rgbaBytes(plan.output);
            return plan;
        }
        case DecodeScaling::Dct: {
            // The biggest reduction that still gives at least the target size keeps the quality.
            int start = 1;
            for (const int d : {8, 4, 2}) {
                const ImageSize scaled = scaledBy(image, d);
                if (scaled.width >= target.width && scaled.height >= target.height) {
                    start = d;
                    break;
                }
            }
            for (int d = start; d <= 8; d *= 2) {
                DecodePlan plan = shrinkWithin(scaledBy(image, d), target, budget, extraBytes, slackPercent);
                if (plan.ok) {
                    plan.denominator = d;
                    return plan;
                }
            }
            return {};
        }
    }
    return {};
}

std::vector<TileRect> planTiles(ImageSize image, int maxSide) {
    std::vector<TileRect> tiles;
    if (empty(image) || maxSide < 1) {
        return tiles;
    }
    const int tileWidth = ceilDiv(image.width, ceilDiv(image.width, maxSide));
    const int tileHeight = ceilDiv(image.height, ceilDiv(image.height, maxSide));
    for (int y = 0; y < image.height; y += tileHeight) {
        for (int x = 0; x < image.width; x += tileWidth) {
            tiles.push_back(TileRect{x, y, std::min(tileWidth, image.width - x), std::min(tileHeight, image.height - y)});
        }
    }
    return tiles;
}

TileRange tileWindow(const std::vector<TileRect>& tiles, int viewTop, int viewBottom, int around) {
    const std::vector<TileRow> rows = rowsOf(tiles);
    if (rows.empty()) {
        return {};
    }
    viewBottom = std::max(viewBottom, viewTop + 1);  // an empty view counts as one line
    const int count = static_cast<int>(rows.size());
    int firstRow = -1;
    int lastRow = -1;
    for (int r = 0; r < count; ++r) {
        const TileRow& row = rows[static_cast<std::size_t>(r)];
        if (row.top < viewBottom && row.bottom > viewTop) {
            if (firstRow < 0) {
                firstRow = r;
            }
            lastRow = r;
        }
    }
    if (firstRow < 0) {
        // Nothing in view (above the first row, below the last, or in a gap): the nearest row stands in for the view.
        int nearest = 0;
        int nearestDistance = -1;
        for (int r = 0; r < count; ++r) {
            const TileRow& row = rows[static_cast<std::size_t>(r)];
            const int distance = row.bottom <= viewTop ? viewTop - row.bottom + 1 : row.top - viewBottom + 1;
            if (nearestDistance < 0 || distance < nearestDistance) {
                nearest = r;
                nearestDistance = distance;
            }
        }
        firstRow = lastRow = nearest;
    }
    around = std::max(around, 0);
    firstRow = std::max(0, firstRow - around);
    lastRow = std::min(count - 1, lastRow + around);
    return {rows[static_cast<std::size_t>(firstRow)].first, rows[static_cast<std::size_t>(lastRow)].last};
}

}  // namespace snx::core
