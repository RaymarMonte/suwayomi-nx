#include "ui/components/GridLayout.hpp"

#include <algorithm>
#include <cmath>

namespace snx::ui {

GridLayout::GridLayout(float width, GridMetrics metrics) : metrics_(metrics) {
    const float step = metrics.cellWidth + metrics.spacing;
    const float usable = width - 2 * metrics.padding;
    if (step > 0 && usable > 0) {
        // n cells need n * cellWidth + (n - 1) * spacing.
        columns_ = std::max(1, static_cast<int>(std::floor((usable + metrics.spacing) / step)));
    }
    const float used = columns_ * metrics.cellWidth + (columns_ - 1) * metrics.spacing;
    left_ = metrics.padding + std::max(0.0f, (usable - used) / 2);
}

std::size_t GridLayout::rows(std::size_t count) const {
    const auto columns = static_cast<std::size_t>(columns_);
    return (count + columns - 1) / columns;
}

float GridLayout::rowTop(std::size_t row) const {
    return metrics_.padding + static_cast<float>(row) * (metrics_.cellHeight + metrics_.spacing);
}

float GridLayout::contentHeight(std::size_t count) const {
    const std::size_t rowCount = rows(count);
    if (rowCount == 0) {
        return 2 * metrics_.padding;
    }
    return rowTop(rowCount - 1) + metrics_.cellHeight + metrics_.padding;
}

GridLayout::Origin GridLayout::origin(std::size_t index) const {
    const auto columns = static_cast<std::size_t>(columns_);
    const std::size_t column = index % columns;
    return {left_ + static_cast<float>(column) * (metrics_.cellWidth + metrics_.spacing), rowTop(index / columns)};
}

GridLayout::Range GridLayout::visible(float scrollTop, float viewHeight, std::size_t count, int extraRows) const {
    const std::size_t rowCount = rows(count);
    const float rowStep = metrics_.cellHeight + metrics_.spacing;
    if (rowCount == 0 || rowStep <= 0 || viewHeight <= 0) {
        return {};
    }
    // A row shows while any part of it is between scrollTop and scrollTop + viewHeight.
    const float top = std::max(0.0f, scrollTop - metrics_.padding);
    const float bottom = std::max(0.0f, scrollTop + viewHeight - metrics_.padding);
    auto firstRow = static_cast<std::size_t>(std::floor((top + metrics_.spacing) / rowStep));
    auto endRow = static_cast<std::size_t>(std::ceil(bottom / rowStep)) + static_cast<std::size_t>(std::max(0, extraRows));
    firstRow = std::min(firstRow, rowCount);
    endRow = std::min(std::max(endRow, firstRow), rowCount);
    const auto columns = static_cast<std::size_t>(columns_);
    return {std::min(firstRow * columns, count), std::min(endRow * columns, count)};
}

std::optional<std::size_t> GridLayout::neighbour(std::size_t index, GridMove move, std::size_t count) const {
    if (index >= count) {
        return std::nullopt;
    }
    const auto columns = static_cast<std::size_t>(columns_);
    const std::size_t column = index % columns;
    switch (move) {
        case GridMove::Left:
            if (column == 0) {
                return std::nullopt;
            }
            return index - 1;
        case GridMove::Right:
            if (column + 1 == columns || index + 1 >= count) {
                return std::nullopt;
            }
            return index + 1;
        case GridMove::Up:
            if (index < columns) {
                return std::nullopt;
            }
            return index - columns;
        case GridMove::Down:
            if (index / columns + 1 >= rows(count)) {
                return std::nullopt;
            }
            return std::min(index + columns, count - 1);
    }
    return std::nullopt;
}

}  // namespace snx::ui
