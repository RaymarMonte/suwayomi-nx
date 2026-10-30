#pragma once

#include <borealis.hpp>
#include <chrono>
#include <functional>
#include <vector>

#include "core/AppConfig.hpp"
#include "core/ReaderMath.hpp"
#include "image/Caches.hpp"
#include "image/DecodedImage.hpp"

// The reader's page area (WP-11): fills the screen, draws one decoded page from its GPU tiles (one texture per tile, SAFETY.md
// S7: reserved in the TextureBudget) at the size of the fit, and turns touch into reader input (D16): a tap reports its zone
// (left / middle / right), a drag moves a page that is bigger than the screen, and a sideways swipe reports the side to turn
// to (only when the page was already at its edge that way). The activity's views (messages, page number, menu bars) go into
// overlay() and draw on top.
// Portrait (WP-22): everything (page, spinner, overlay) is drawn inside one NanoVG transform (core::readerTransform), the
// overlay laid out for the reader's screen (720 x 1280 in handheld). borealis finds touched views by their unturned boxes, so
// in portrait this view takes every touch itself (hitTest), maps it with core::screenToReader, and runs the action of an
// addButton() button under the mapped point; anything else is a tap zone, a drag or a swipe on the reader's screen.
// Main thread only.
namespace snx::ui {

class ReaderView : public brls::Box {
  public:
    explicit ReaderView(image::TextureBudget& textures);
    ~ReaderView() override;  // frees the textures

    // The page to draw (nullptr: none), its fit, and the reading direction (where a page wider than the screen starts). A
    // new image or fit starts at the page's start position (the top; core::startPosition); the same one stays where it is.
    void showPage(const image::ImagePtr& image, core::PageFit fit, core::ReadingDirection direction);
    // The GPU budget had no room for the page (S7): it isn't drawn.
    bool textureRefused() const { return refused_; }
    bool hasPage() const { return !textures_.empty(); }
    // Goes up with every new page image shown (the activity's test events tell one page image from the next).
    unsigned pageRevision() const { return revision_; }
    // Moves a page taller than the screen up or down (D-pad), in screen units.
    void scrollBy(float dy);
    // The drawn page's size, for the test events. {0, 0} without a page.
    core::SizeF pageSize() const { return drawn_; }
    // Where the page's top-left corner is on the reader's screen, and a count of finished moves (drags, scrolls), for the
    // test events: a move changes no page, so the activity reports it by this count.
    core::PointF pagePosition() const { return position_; }
    unsigned moveRevision() const { return moves_; }

    void setSpinner(bool on) { spinner_ = on; }  // the page is loading: a turning arc in the middle

    // The box the activity's views go into: the reader's screen, laid out upright for the orientation.
    brls::Box* overlay() const { return overlay_; }
    // How the console is held; `screen` = this view's real (landscape) size in units. The page starts again at its start.
    void setOrientation(core::ReaderOrientation orientation, core::SizeF screen);
    core::ReaderOrientation orientation() const { return orientation_; }
    // The reader's screen in units (720 x 1280 in portrait handheld).
    core::SizeF readerScreen() const { return core::readerScreen(orientation_, real_); }
    // A button in the overlay: in portrait a mapped tap on it runs `action` (borealis can't find it there). In landscape
    // borealis runs the button's own click action as usual.
    void addButton(brls::View* button, std::function<void()> action);

    std::function<void(core::TapArea)> onTap;
    std::function<void(core::Side)> onSwipe;
    std::function<void()> onDrawn;  // after every frame it drew (the activity writes its test events there)

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;
    brls::View* hitTest(brls::Point point) override;
    // borealis skips drawing a child outside its parents' boxes: in portrait the box is the reader's screen (720 x 1280).
    void getCullingBounds(float* top, float* right, float* bottom, float* left) override;

  private:
    struct Button {
        brls::View* view;
        std::function<void()> action;
    };

    void freeTextures();
    void layout(core::SizeF screen);
    void onTapped(brls::Point position);
    void onPan(const brls::PanGestureStatus& status);
    core::PointF toReader(brls::Point position);
    bool shown(brls::View* view);
    void drawPage(NVGcontext* vg, float x, float y, core::SizeF screen);
    void drawSpinner(NVGcontext* vg, float cx, float cy);

    image::TextureBudget& budget_;
    image::ImagePtr image_;
    std::vector<int> textures_;  // one per tile, in image_->tiles() order
    std::vector<image::TextureLease> leases_;
    bool refused_ = false;
    unsigned revision_ = 0;
    unsigned moves_ = 0;
    core::PageFit fit_ = core::PageFit::Screen;
    core::ReadingDirection direction_ = core::ReadingDirection::RightToLeft;
    bool placed_ = false;  // position_ is valid for the current page, fit and screen
    core::SizeF screen_;
    core::SizeF drawn_;
    core::PointF position_;
    // The touch under way: where and when the finger touched down, and the drag (if borealis calls it one).
    brls::Point touchDown_;
    std::chrono::steady_clock::time_point touchDownAt_;
    core::PointF dragFrom_;
    bool couldMoveLeft_ = false;
    bool couldMoveRight_ = false;
    bool spinner_ = false;
    brls::Box* overlay_ = nullptr;  // a child: borealis deletes it
    core::ReaderOrientation orientation_ = core::ReaderOrientation::Landscape;
    core::SizeF real_{1280, 720};
    std::vector<Button> buttons_;
};

}  // namespace snx::ui
