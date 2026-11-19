#include "ui/components/GridLayout.hpp"

#include <algorithm>
#include <cmath>

namespace snx::ui {

GridLayout::GridLayout(float width, GridMetrics metrics) : metrics_(metrics) {
    const float step = metrics.cellWidth + metrics.spacing;
    const float usable = width - 2 * metrics.padding;
    if (metrics.columns > 0) {
        columns_ = metrics.columns;  // the screen chose the cell width for this many
    } else if (step > 0 && usable > 0) {
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

std::optional<std::size_t> GridLayout::inRow(std::size_t row, std::size_t column, std::size_t count,
                                             const GridFocusable& focusable) const {
    const auto columns = static_cast<std::size_t>(columns_);
    const std::size_t first = row * columns;
    if (first >= count) {
        return std::nullopt;
    }
    const std::size_t last = std::min(first + columns, count) - 1;  // the row's last cell
    const std::size_t start = std::min(first + column, last);
    for (std::size_t i = start + 1; i-- > first;) {  // at or before the column, nearest first
        if (!focusable || focusable(i)) {
            return i;
        }
    }
    for (std::size_t i = start + 1; i <= last; ++i) {  // else after it
        if (focusable(i)) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> GridLayout::neighbour(std::size_t index, GridMove move, std::size_t count,
                                                 const GridFocusable& focusable) const {
    if (index >= count) {
        return std::nullopt;
    }
    const auto columns = static_cast<std::size_t>(columns_);
    const std::size_t column = index % columns;
    const std::size_t row = index / columns;
    const std::size_t rowStart = row * columns;
    const std::size_t rowEnd = std::min(rowStart + columns, count);  // one past the row's last cell
    switch (move) {
        case GridMove::Left:
            for (std::size_t i = index; i-- > rowStart;) {
                if (!focusable || focusable(i)) {
                    return i;
                }
            }
            return std::nullopt;
        case GridMove::Right:
            for (std::size_t i = index + 1; i < rowEnd; ++i) {
                if (!focusable || focusable(i)) {
                    return i;
                }
            }
            return std::nullopt;
        case GridMove::Up:
            for (std::size_t r = row; r-- > 0;) {
                if (const auto found = inRow(r, column, count, focusable)) {
                    return found;
                }
            }
            return std::nullopt;
        case GridMove::Down:
            for (std::size_t r = row + 1; r < rows(count); ++r) {
                if (const auto found = inRow(r, column, count, focusable)) {
                    return found;
                }
            }
            return std::nullopt;
    }
    return std::nullopt;
}

std::size_t GridLayout::focusableNear(std::size_t index, std::size_t count, const GridFocusable& focusable) const {
    if (!focusable || index >= count || focusable(index)) {
        return index;
    }
    const auto columns = static_cast<std::size_t>(columns_);
    const auto found = inRow(index / columns, index % columns, count, focusable);
    return found ? *found : index;
}

}  // namespace snx::ui
