#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>

#include "core/AppConfig.hpp"
#include "core/ReaderMath.hpp"
#include "image/Caches.hpp"
#include "image/DecodedImage.hpp"

// The reader's page area (WP-11): fills the screen, draws one decoded page from its GPU tiles (one texture per tile, SAFETY.md
// S7: reserved in the TextureBudget) at the size of the fit, and turns touch into reader input (D16): a tap reports its zone
// (left / middle / right), a drag moves a page that is bigger than the screen, and a sideways swipe reports the side to turn
// to (only when the page was already at its edge that way). Its children (messages, page number, menu bars) draw on top.
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

    void setSpinner(bool on) { spinner_ = on; }  // the page is loading: a turning arc in the middle

    std::function<void(core::TapArea)> onTap;
    std::function<void(core::Side)> onSwipe;
    std::function<void()> onDrawn;  // after every frame it drew (the activity writes its test events there)

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    void freeTextures();
    void layout(core::SizeF screen);
    void onPan(const brls::PanGestureStatus& status);
    void drawSpinner(NVGcontext* vg, float cx, float cy);

    image::TextureBudget& budget_;
    image::ImagePtr image_;
    std::vector<int> textures_;  // one per tile, in image_->tiles() order
    std::vector<image::TextureLease> leases_;
    bool refused_ = false;
    unsigned revision_ = 0;
    core::PageFit fit_ = core::PageFit::Screen;
    core::ReadingDirection direction_ = core::ReadingDirection::RightToLeft;
    bool placed_ = false;  // position_ is valid for the current page, fit and screen
    core::SizeF screen_;
    core::SizeF drawn_;
    core::PointF position_;
    // The drag under way.
    core::PointF dragFrom_;
    bool couldMoveLeft_ = false;
    bool couldMoveRight_ = false;
    bool spinner_ = false;
};

}  // namespace snx::ui
