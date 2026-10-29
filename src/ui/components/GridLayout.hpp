#pragma once

#include <cstddef>
#include <optional>

// The arithmetic of a grid of equal cells (WP-09): how many columns fit, where cell N goes, which cells a scroll position
// shows, and where the focus moves. Without borealis, so it is unit-tested; CoverGrid (the view) uses it.
namespace snx::ui {

enum class GridMove { Up, Down, Left, Right };

struct GridMetrics {
    float cellWidth = 0;
    float cellHeight = 0;
    float spacing = 0;  // between cells, both ways
    float padding = 0;  // around the whole grid
};

class GridLayout {
  public:
    GridLayout() = default;
    GridLayout(float width, GridMetrics metrics);

    int columns() const { return columns_; }  // at least 1
    std::size_t rows(std::size_t count) const;
    float contentHeight(std::size_t count) const;  // with padding; just the padding when count is 0
    float rowTop(std::size_t row) const;

    struct Origin {
        float x = 0;
        float y = 0;
    };
    // The cell's top-left corner. The columns are centred in the width.
    Origin origin(std::size_t index) const;

    // The cells [first, last) whose rows show between scrollTop and scrollTop + viewHeight, plus `extraRows` more rows below.
    struct Range {
        std::size_t first = 0;
        std::size_t last = 0;
        bool contains(std::size_t index) const { return index >= first && index < last; }
    };
    Range visible(float scrollTop, float viewHeight, std::size_t count, int extraRows = 0) const;

    // Where the focus goes from `index`. nullopt: the move leaves the grid (up from the first row, left from the first
    // column, right from the last cell of a row). Down into a shorter last row lands on its last cell.
    std::optional<std::size_t> neighbour(std::size_t index, GridMove move, std::size_t count) const;

  private:
    GridMetrics metrics_;
    int columns_ = 1;
    float left_ = 0;  // x of the first column
};

}  // namespace snx::ui
