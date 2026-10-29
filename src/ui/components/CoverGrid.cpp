#include "ui/components/CoverGrid.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace snx::ui {

// The scrolling content: the cells sit in it at fixed places (borealis "detached" views, outside its flex layout), and it
// asks the grid where the focus goes, because a flex box would only know "the next child".
class CoverGrid::Content : public brls::Box {
  public:
    explicit Content(CoverGrid& grid) : brls::Box(brls::Axis::COLUMN), grid_(grid) {}

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override {
        return grid_.nextFocus(direction, currentView);
    }
    // Box's own answer would be the last focused child, which may be a cell back in the pool.
    brls::View* getDefaultFocus() override { return grid_.focusCell(); }

  private:
    CoverGrid& grid_;
};

CoverGrid::CoverGrid(GridMetrics metrics) : metrics_(metrics) {
    setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    content_ = new Content(*this);
    setContentView(content_);
}

CoverGrid::~CoverGrid() {
    // The cells in the content are deleted with it (borealis deletes a box's children); the pool is ours.
    for (GridCell* cell : pool_) {
        delete cell;
    }
}

void CoverGrid::setSource(GridSource* source) {
    if (source_ != nullptr) {
        releaseAll();
    }
    source_ = source;
    if (source_ != nullptr) {
        reload(0);
    }
}

bool CoverGrid::hasFocus() const {
    const brls::View* focused = brls::Application::getCurrentFocus();
    for (const brls::View* child : content_->getChildren()) {
        if (child == focused) {
            return true;
        }
    }
    return false;
}

void CoverGrid::reload(std::size_t focus) {
    const bool hadFocus = hasFocus();
    releaseAll();
    const std::size_t count = source_ ? source_->count() : 0;
    focus_ = count == 0 ? 0 : std::min(focus, count - 1);
    if (layoutWidth_ <= 0) {
        return;  // not laid out yet: onLayout() comes back here
    }
    content_->setHeight(layout_.contentHeight(count));
    scrollTo(focus_);
    recycle();
    if (hadFocus && count > 0) {
        brls::Application::giveFocus(focusCell());
    }
}

void CoverGrid::appended() {
    if (source_ == nullptr || layoutWidth_ <= 0) {
        return;
    }
    content_->setHeight(layout_.contentHeight(source_->count()));
    recycle();
}

void CoverGrid::scrollTo(std::size_t index) {
    const std::size_t count = source_ ? source_->count() : 0;
    const float contentHeight = layout_.contentHeight(count);
    const auto row = static_cast<std::size_t>(index / static_cast<std::size_t>(layout_.columns()));
    float offset = layout_.rowTop(row) + metrics_.cellHeight / 2 - getHeight() / 2;
    offset = std::min(offset, contentHeight - getHeight());
    setContentOffsetY(std::max(0.0f, offset), false);
}

void CoverGrid::onLayout() {
    brls::ScrollingFrame::onLayout();
    const float width = getWidth();
    if (width <= 0 || std::abs(width - layoutWidth_) < 0.5f) {
        return;
    }
    layoutWidth_ = width;
    layout_ = GridLayout(width, metrics_);
    content_->setWidth(width);
    reload(focus_);
}

void CoverGrid::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                     brls::FrameContext* ctx) {
    recycle();
    brls::ScrollingFrame::draw(vg, x, y, width, height, style, ctx);
}

GridCell* CoverGrid::findCell(std::size_t index) const {
    for (brls::View* child : content_->getChildren()) {
        auto* cell = static_cast<GridCell*>(child);
        if (cell->index_ == index) {
            return cell;
        }
    }
    return nullptr;
}

void CoverGrid::recycle() {
    if (source_ == nullptr || layoutWidth_ <= 0) {
        return;
    }
    const std::size_t count = source_->count();
    const GridLayout::Range range = layout_.visible(getContentOffsetY(), getHeight(), count);
    const brls::View* focused = brls::Application::getCurrentFocus();
    // Copy: release() changes the list.
    const std::vector<brls::View*> children = content_->getChildren();
    for (brls::View* child : children) {
        auto* cell = static_cast<GridCell*>(child);
        if ((!range.contains(cell->index_) || cell->index_ >= count) && cell != focused) {
            release(cell);
        }
    }
    for (std::size_t i = range.first; i < range.last; ++i) {
        cellFor(i);
    }
    if (range.first != shown_.first || range.last != shown_.last) {
        shown_ = range;
        source_->showing(range.first, range.last);
    }
}

GridCell* CoverGrid::cellFor(std::size_t index) {
    if (GridCell* found = findCell(index)) {
        return found;
    }
    GridCell* cell = nullptr;
    if (pool_.empty()) {
        cell = source_->makeCell();
        cell->detach();
        cell->setFocusable(true);
        // A (and a tap, through the tap recognizer) selects the cell's item, whichever item it shows now.
        cell->registerClickAction([this, cell](brls::View*) {
            if (source_ != nullptr) {
                source_->select(cell->index_);
            }
            return true;
        });
        cell->addGestureRecognizer(new brls::TapGestureRecognizer(cell));
    } else {
        cell = pool_.back();
        pool_.pop_back();
    }
    cell->index_ = index;
    const GridLayout::Origin origin = layout_.origin(index);
    cell->setWidth(metrics_.cellWidth);
    cell->setHeight(metrics_.cellHeight);
    cell->setDetachedPosition(origin.x, origin.y);
    content_->getChildren().push_back(cell);
    cell->setParent(content_);
    source_->bind(*cell, index);
    cell->View::willAppear(true);
    cell->invalidate();  // a detached view lays itself out
    return cell;
}

void CoverGrid::release(GridCell* cell) {
    auto& children = content_->getChildren();
    const auto it = std::find(children.begin(), children.end(), cell);
    if (it == children.end()) {
        return;
    }
    if (source_ != nullptr) {
        source_->unbind(*cell);
    }
    children.erase(it);
    cell->willDisappear(true);
    pool_.push_back(cell);
}

void CoverGrid::releaseAll() {
    const std::vector<brls::View*> children = content_->getChildren();
    for (brls::View* child : children) {
        release(static_cast<GridCell*>(child));
    }
    shown_ = {};
}

brls::View* CoverGrid::focusCell() {
    if (source_ == nullptr || source_->count() == 0 || layoutWidth_ <= 0) {
        return nullptr;
    }
    return cellFor(std::min(focus_, source_->count() - 1));
}

brls::View* CoverGrid::getDefaultFocus() {
    if (getVisibility() != brls::Visibility::VISIBLE) {
        return nullptr;  // hidden behind a status: the status takes the focus
    }
    brls::View* cell = focusCell();
    return cell != nullptr ? cell : brls::ScrollingFrame::getDefaultFocus();
}

void CoverGrid::onChildFocusGained(brls::View* directChild, brls::View* focusedView) {
    for (brls::View* child : content_->getChildren()) {
        if (child == focusedView) {
            focus_ = static_cast<GridCell*>(child)->index_;
            if (source_ != nullptr) {
                source_->focused(focus_);
            }
            break;
        }
    }
    brls::ScrollingFrame::onChildFocusGained(directChild, focusedView);  // scrolls the focused row to the middle
}

brls::View* CoverGrid::nextFocus(brls::FocusDirection direction, brls::View* current) {
    std::optional<std::size_t> next;
    if (source_ != nullptr && current != nullptr && current->getParent() == content_) {
        GridMove move = GridMove::Down;
        switch (direction) {
            case brls::FocusDirection::UP:
                move = GridMove::Up;
                break;
            case brls::FocusDirection::DOWN:
                move = GridMove::Down;
                break;
            case brls::FocusDirection::LEFT:
                move = GridMove::Left;
                break;
            case brls::FocusDirection::RIGHT:
                move = GridMove::Right;
                break;
        }
        next = layout_.neighbour(static_cast<GridCell*>(current)->index_, move, source_->count());
    }
    if (next) {
        return cellFor(*next);  // made now if its row isn't on screen yet; the focus scrolls it into view
    }
    // Out of the grid (the category bar above, the tab list on the left). Like RecyclerFrame: straight to the parent, past
    // ScrollingFrame::getNextFocus (it would keep the focus here while it can still scroll).
    brls::View* outside = getParentNavigationDecision(this, nullptr, direction);
    if (outside == nullptr && hasParent()) {
        outside = getParent()->getNextFocus(direction, this);
    }
    return outside;
}

}  // namespace snx::ui
