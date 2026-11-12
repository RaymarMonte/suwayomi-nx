#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/ReaderMath.hpp"
#include "core/StripMath.hpp"
#include "image/Caches.hpp"
#include "ui/reader/StripModel.hpp"

// Draws the webtoon strip (WP-13b) inside ReaderView, which keeps the screen, the portrait transform, the overlay, the
// buttons and the touch handling for both readers (ReaderView::setStrip). The strip is centred on the reader's screen at
// StripModel::stripWidth(); its pages are drawn from GPU tiles (one texture per tile, each reserved in the TextureBudget, S7).
// The tiles kept are core::stripTileWindow's (the rows on screen plus RuntimeProfile::webtoonTilesAround rows above and
// below); the ones on screen upload at once, the others at most one per frame (nearest first), and the ones that leave are
// freed (if the budget refuses one, the farthest go first). A page still loading is a placeholder with the spinner; a failed
// page and the bands between chapters show their text (the activity gives it, in the app's language).
// It also runs the strip's motion from the clock: the flick after a drag, the glides of the D-pad and taps; the held D-pad
// and the sticks scroll it by scroll(). Main thread only.
namespace snx::ui {

class StripCanvas {
  public:
    StripCanvas(StripModel& model, image::TextureBudget& budget, int tilesAround);
    ~StripCanvas();  // frees the textures

    StripCanvas(const StripCanvas&) = delete;
    StripCanvas& operator=(const StripCanvas&) = delete;

    // The lines of text for a band or a failed page at layout item `item` (the activity's i18n texts).
    std::function<std::vector<std::string>(int item)> textFor;
    // A gesture began or ended: "fling", "glide" (the activity measures its frames and writes "gesture" events).
    std::function<void(const char* kind, bool started)> onGesture;
    void setTextColor(NVGcolor color) { textColor_ = color; }

    // Once per frame, before draw(): the flick and the glide move on.
    void tick();
    // The strip on the reader's screen (reader units from (x, y)).
    void draw(NVGcontext* vg, float x, float y, core::SizeF screen);

    // --- Touch (ReaderView) ---
    // A finger touched down: a flick or glide stops. True if one was running (that touch is then no tap).
    bool touchDown();
    // A drag: the finger's offset from where the drag began (reader units, + = down), at `ms`. The strip follows it.
    void dragStart(std::int64_t ms);
    void dragTo(float offsetY, std::int64_t ms);
    // The finger lifted: a flick carries on if it was moving (core::releaseVelocity).
    void dragEnd(std::int64_t ms);

    // --- Controller and taps (the activity) ---
    // A glide of core::kStripGlideShare of a screen: +1 on (down the strip), -1 back. A press during a glide adds to it.
    void glide(int direction);
    // Moves the strip at once by `dy` units (held D-pad, sticks): a flick or glide stops. Returns how far it moved.
    double scroll(double dy);
    bool moving() const { return fling_.has_value() || glide_.has_value(); }

    // --- For the test events ---
    int tilesHeld() const { return static_cast<int>(held_.size()); }
    // Frames since the last call in which part of the screen showed a page (or a tile) still loading.
    int takeWaitFrames();
    float uploadMs() const { return uploadMs_; }  // the GPU uploads in the last frame that made any
    bool refused() const { return refused_; }     // the budget had no room for a tile on screen in the last frame
    // Goes up with every finished move (drag, flick, glide, scroll stop), like ReaderView::moveRevision.
    unsigned moveRevision() const { return moves_; }
    void finishScroll() { ++moves_; }

  private:
    struct Key {
        int chapterId = 0;
        int page = 0;
        int tile = 0;
        bool operator<(const Key& other) const {
            if (chapterId != other.chapterId) {
                return chapterId < other.chapterId;
            }
            return page != other.page ? page < other.page : tile < other.tile;
        }
    };
    struct Held {
        int texture = 0;
        image::TextureLease lease;
        core::ImageSize decoded;  // the image it was made from (a new decode of the page has another size: made again)
    };
    struct Fling {
        double velocity = 0;   // units per second at the start
        std::int64_t startMs = 0;
        double done = 0;       // the distance already scrolled
    };
    struct Glide {
        double distance = 0;
        std::int64_t startMs = 0;
        double done = 0;
    };

    void updateTextures(NVGcontext* vg);
    bool upload(NVGcontext* vg, const Key& key, const image::ImagePtr& image, std::size_t tile,
                const std::vector<Key>& keepOrder);
    void freeTexture(NVGcontext* vg, std::map<Key, Held>::iterator it);
    void drawItem(NVGcontext* vg, int item, float left, float top, float width, float height, core::SizeF screen,
                  float y, bool& waiting);
    void drawText(NVGcontext* vg, const std::vector<std::string>& lines, float cx, float cy, float maxWidth);
    void drawSpinner(NVGcontext* vg, float cx, float cy);
    void stopMotion();
    static std::int64_t nowMs();

    StripModel& model_;
    image::TextureBudget& budget_;
    int tilesAround_;
    std::map<Key, Held> held_;
    NVGcolor textColor_ = nvgRGB(200, 200, 200);
    std::vector<core::TouchSample> samples_;  // the drag's last moments
    float dragOffset_ = 0;
    std::optional<Fling> fling_;
    std::optional<Glide> glide_;
    int waitFrames_ = 0;
    float uploadMs_ = 0;
    bool refused_ = false;
    unsigned moves_ = 0;
};

}  // namespace snx::ui
