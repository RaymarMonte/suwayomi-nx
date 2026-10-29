#include "ui/components/CoverImage.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "image/ImageLoader.hpp"

namespace snx::ui {

namespace {
const NVGcolor kPlaceholder = nvgRGB(60, 60, 64);
}

CoverImage::CoverImage(float width, float height) : width_(width), height_(height) {
    setWidth(width);
    setHeight(height);
    setBackgroundColor(kPlaceholder);
    setCornerRadius(4);
    setClipsToBounds(true);
    image_ = new brls::Image();
    image_->setWidth(width);
    image_->setHeight(height);
    image_->setScalingType(brls::ImageScalingType::FILL);
    image_->setFreeTexture(true);
    addView(image_);
}

CoverImage::~CoverImage() {
    cancel_.cancel();
}

core::ImageSize CoverImage::decodeBox(float width, float height) {
    const float scale = std::max(1.0f, brls::Application::windowScale);
    return {static_cast<int>(width * scale), static_cast<int>(height * scale)};
}

void CoverImage::load(const std::string& url, app::AppContext& context) {
    if (url == url_) {
        return;  // already showing (or loading) it
    }
    clear();
    url_ = url;
    if (url.empty()) {
        return;
    }
    const image::LoadRequest request{url, decodeBox(width_, height_), net::Priority::Interactive, image::ImageKind::Cover};
    if (image::ImagePtr ready = context.images.cached(request.url, request.box, image::ImageKind::Cover)) {
        show(ready, context.textures);
        return;
    }
    cancel_ = net::CancelToken();
    const net::CancelToken token = cancel_;
    image::TextureBudget* textures = &context.textures;
    context.images.load(request, token, [this, token, textures](const image::ImageResult& result) {
        if (token.cancelled()) {
            return;  // the view is gone, or shows another cover
        }
        if (result.ok()) {
            show(result.value(), *textures);
        }
    });
}

void CoverImage::clear() {
    cancel_.cancel();
    url_.clear();
    image_->clear();
    lease_.reset();
}

void CoverImage::show(const image::ImagePtr& decoded, image::TextureBudget& textures) {
    if (!decoded || decoded->tiles().empty()) {
        return;
    }
    // A cover decoded for this box is one tile (far below maxTextureSide).
    const core::TileRect& tile = decoded->tiles()[0];
    image::TextureLease lease(textures, image::TextureBudget::bytesFor(tile));
    if (!lease.ok()) {
        return;  // the GPU budget is full (S7): keep the placeholder
    }
    std::vector<std::uint8_t> scratch;
    const std::uint8_t* pixels = decoded->tilePixels(0, scratch);
    const int texture =
        pixels ? nvgCreateImageRGBA(brls::Application::getNVGContext(), tile.width, tile.height, 0, pixels) : 0;
    if (texture == 0) {
        return;
    }
    image_->clear();
    lease_ = std::move(lease);
    image_->innerSetImage(texture);
}

}  // namespace snx::ui
