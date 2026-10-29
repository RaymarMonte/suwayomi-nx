#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <vector>

#include "ui/components/GridLayout.hpp"

// A scrolling grid of equal cells that reuses its cells (WP-09): only the rows on screen have cells, so a library of
// thousands of manga costs the same as one screen of them. The structure (cells placed by hand inside the scrolling content,
// a pool of cells to reuse, a check on every frame) is adapted from borealis's RecyclerFrame (Apache-2.0, CREDITS.md); the
// arithmetic is GridLayout. Controller: the focused row stays in the middle (borealis's CENTERED scrolling). Touch: drag and
// flick scroll the grid, a tap selects a cell. The focused cell is never reused while it has the focus, so the focus always
// points at a real cell. Main thread only, like every view.
namespace snx::ui {

// One cell. The grid sets its size and position; the source fills it.
class GridCell : public brls::Box {
  public:
    std::size_t index() const { return index_; }

    // borealis asks the focused view itself first. A cell is a box, and Box's answer would look up the cell among its own
    // children by an index it doesn't have (a null pointer: the crash found in WP-09). Every move is the grid's to decide.
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override {
        (void)currentView;
        return hasParent() ? getParent()->getNextFocus(direction, this) : nullptr;
    }

  private:
    friend class CoverGrid;
    std::size_t index_ = 0;
};

// What the grid shows (the screen implements it).
class GridSource {
  public:
    virtual ~GridSource() = default;
    virtual std::size_t count() const = 0;
    // A new cell; the grid owns it from now on (deletes it).
    virtual GridCell* makeCell() = 0;
    // Fill `cell` for item `index`; it is about to show (start its cover load here).
    virtual void bind(GridCell& cell, std::size_t index) = 0;
    // `cell` scrolled away or the items changed: stop its loads, free its textures.
    virtual void unbind(GridCell& cell) = 0;
    // A or a tap on item `index`.
    virtual void select(std::size_t index) = 0;
    // Item `index` got the focus.
    virtual void focused(std::size_t index) { (void)index; }
    // The cells on screen are now the items [first, last).
    virtual void showing(std::size_t first, std::size_t last) {
        (void)first;
        (void)last;
    }
};

class CoverGrid : public brls::ScrollingFrame {
  public:
    explicit CoverGrid(GridMetrics metrics);
    ~CoverGrid() override;

    // Not owned. Before the source goes away, call setSource(nullptr): that unbinds every cell while it still can.
    void setSource(GridSource* source);
    // The items changed: every cell is filled again, the grid scrolls to item `focus` and, if the grid had the focus, gives
    // it to that item.
    void reload(std::size_t focus);
    // More items at the end; the cells on screen stay as they are.
    void appended();
    // The items changed in place (same count, same order): the cells on screen are filled again, nothing scrolls.
    void rebind();
    // The focused item (or the one that gets the focus when the grid gets it).
    std::size_t focusIndex() const { return focus_; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;
    void onLayout() override;
    brls::View* getDefaultFocus() override;
    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override;

  private:
    class Content;

    brls::View* nextFocus(brls::FocusDirection direction, brls::View* current);
    brls::View* focusCell();       // the cell for focus_, made if it isn't there
    void recycle();                // cells for the rows on screen, the others back into the pool
    GridCell* cellFor(std::size_t index);  // the cell showing `index`, made if needed
    GridCell* findCell(std::size_t index) const;
    void release(GridCell* cell);  // unbind it, take it out of the content, into the pool
    void releaseAll();
    bool hasFocus() const;         // the focus is on one of the cells
    void scrollTo(std::size_t index);

    GridMetrics metrics_;
    GridLayout layout_;
    float layoutWidth_ = -1;
    Content* content_ = nullptr;
    GridSource* source_ = nullptr;
    std::vector<GridCell*> pool_;
    std::size_t focus_ = 0;
    GridLayout::Range shown_;
};

}  // namespace snx::ui
