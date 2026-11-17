#pragma once

#include <borealis.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "app/AppContext.hpp"
#include "core/models/Manga.hpp"
#include "image/ImageLoader.hpp"
#include "ui/components/CoverGrid.hpp"

// One cover in a cover grid (WP-09; shared since WP-14, D56): the image (a grey placeholder until it arrives), the unread
// badge on it (top left), an optional tag (bottom left, e.g. "In library" in a source's grid) and the title below in two
// lines. The library, a source's screen and (WP-14b) Search all use it. Main thread only, like every view.
namespace snx::ui {

// The cell sizes (D42): 6 columns fit the Library tab (1280 - 410 px sidebar - 2 x 24 padding); a cell is far over the
// 72 px touch minimum. Covers are 2:3.
constexpr float kCoverWidth = 120;
constexpr float kCoverHeight = 180;
constexpr float kCoverTitleGap = 6;
constexpr float kCoverTitleHeight = 44;  // two lines
constexpr GridMetrics kCoverGridMetrics{kCoverWidth, kCoverHeight + kCoverTitleGap + kCoverTitleHeight, 16, 24};

class CoverCell : public GridCell {
  public:
    CoverCell();
    ~CoverCell() override;

    // url: the cover's full URL (empty: the placeholder stays). tag: empty = none. `context` must outlive the cell.
    void show(const core::Manga& manga, std::string url, app::AppContext& context, const std::string& tag = {});
    void clear();  // scrolled away: cancels the load, frees the texture

    // The size to decode covers at: the cover in screen pixels.
    static core::ImageSize coverBox();

  private:
    void setCover(const image::ImagePtr& decoded, image::TextureBudget& textures);

    brls::Box* cover_;
    brls::Image* image_;
    brls::Box* badgeBox_;
    brls::Label* badge_;
    brls::Box* tagBox_;
    brls::Label* tag_;
    brls::Label* title_;
    net::CancelToken cancel_;
    image::TextureLease lease_;
};

// The row below the screen (RuntimeProfile::coverRowPrefetch): items [from, from + count) load at Prefetch priority into
// the cover cache, so scrolling down finds them ready. `token` cancels them (the next call's caller makes a new one).
void prefetchCovers(app::AppContext& context, const std::vector<core::Manga>& items, std::size_t from, std::size_t count,
                    const net::CancelToken& token);

}  // namespace snx::ui
