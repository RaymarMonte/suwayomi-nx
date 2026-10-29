#pragma once

#include <borealis.hpp>
#include <string>

#include "app/AppContext.hpp"
#include "core/ImageGeometry.hpp"
#include "image/Caches.hpp"
#include "net/CancelToken.hpp"

// A manga cover of a fixed size (WP-10, the details screen): a grey placeholder until the image is there. The image comes
// from the cover cache when it is in it (the library grid decodes covers at the same size), else it loads at Interactive
// priority. Its texture is reserved in AppContext::textures (S7) and freed with the view or clear(). Main thread only.
namespace snx::ui {

class CoverImage : public brls::Box {
  public:
    CoverImage(float width, float height);
    ~CoverImage() override;

    // Shows the cover at `url` (a full URL; empty: just the placeholder). `context` must outlive this view.
    void load(const std::string& url, app::AppContext& context);
    void clear();  // back to the placeholder: cancels the load, frees the texture

    // The size to decode a width x height cover at: in screen pixels.
    static core::ImageSize decodeBox(float width, float height);

  private:
    void show(const image::ImagePtr& decoded, image::TextureBudget& textures);

    float width_;
    float height_;
    brls::Image* image_;
    std::string url_;
    net::CancelToken cancel_;
    image::TextureLease lease_;
};

}  // namespace snx::ui
