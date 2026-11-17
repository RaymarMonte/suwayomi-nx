#include "ui/components/CoverCell.hpp"

#include <algorithm>
#include <utility>

#include "image/Caches.hpp"
#include "ui/components/TextFit.hpp"

namespace snx::ui {

namespace {

constexpr float kTitleFontSize = 16;
constexpr float kTitleLineHeight = 1.25f;
const NVGcolor kPlaceholder = nvgRGB(60, 60, 64);
const NVGcolor kBadge = nvgRGB(0, 150, 200);
const NVGcolor kTag = nvgRGBA(20, 20, 24, 220);

}  // namespace

CoverCell::CoverCell() {
    setAxis(brls::Axis::COLUMN);
    setCornerRadius(4);
    setHighlightCornerRadius(6);

    cover_ = new brls::Box();
    cover_->setWidth(kCoverWidth);
    cover_->setHeight(kCoverHeight);
    cover_->setBackgroundColor(kPlaceholder);
    cover_->setCornerRadius(4);
    cover_->setClipsToBounds(true);
    image_ = new brls::Image();
    image_->setWidth(kCoverWidth);
    image_->setHeight(kCoverHeight);
    image_->setScalingType(brls::ImageScalingType::FILL);
    image_->setFreeTexture(true);
    cover_->addView(image_);

    badgeBox_ = new brls::Box();
    badgeBox_->detach();  // on top of the cover, top left
    badgeBox_->setDetachedPosition(6, 6);
    badgeBox_->setBackgroundColor(kBadge);
    badgeBox_->setCornerRadius(4);
    badgeBox_->setPadding(2, 6, 2, 6);
    badge_ = new brls::Label();
    badge_->setFontSize(14);
    badge_->setTextColor(nvgRGB(255, 255, 255));
    badge_->setAutoAnimate(false);
    badgeBox_->addView(badge_);
    cover_->addView(badgeBox_);

    tagBox_ = new brls::Box();
    tagBox_->detach();  // on top of the cover, bottom left
    tagBox_->setDetachedPosition(6, kCoverHeight - 30);
    tagBox_->setBackgroundColor(kTag);
    tagBox_->setCornerRadius(4);
    tagBox_->setPadding(2, 6, 2, 6);
    tagBox_->setVisibility(brls::Visibility::GONE);
    tag_ = new brls::Label();
    tag_->setFontSize(14);
    tag_->setTextColor(nvgRGB(255, 255, 255));
    tag_->setAutoAnimate(false);
    tagBox_->addView(tag_);
    cover_->addView(tagBox_);
    addView(cover_);

    title_ = new brls::Label();
    title_->setWidth(kCoverWidth);
    title_->setHeight(kCoverTitleHeight);
    title_->setMarginTop(kCoverTitleGap);
    title_->setFontSize(kTitleFontSize);
    title_->setLineHeight(kTitleLineHeight);
    title_->setIsWrapping(true);
    title_->setAutoAnimate(false);
    addView(title_);
}

CoverCell::~CoverCell() {
    cancel_.cancel();
}

void CoverCell::show(const core::Manga& manga, std::string url, app::AppContext& context, const std::string& tag) {
    title_->setText(fitLines(manga.title, kCoverWidth, title_->getFont(), kTitleFontSize, kTitleLineHeight, 2).text);
    if (manga.unreadCount > 0) {
        badge_->setText(std::to_string(manga.unreadCount));
        badgeBox_->setVisibility(brls::Visibility::VISIBLE);
    } else {
        badgeBox_->setVisibility(brls::Visibility::GONE);
    }
    badgeBox_->invalidate();  // detached: lays itself out
    if (tag.empty()) {
        tagBox_->setVisibility(brls::Visibility::GONE);
    } else {
        tag_->setText(tag);
        tagBox_->setVisibility(brls::Visibility::VISIBLE);
    }
    tagBox_->invalidate();
    if (url.empty()) {
        return;
    }
    const image::LoadRequest request{std::move(url), coverBox(), net::Priority::Interactive, image::ImageKind::Cover};
    if (image::ImagePtr ready = context.images.cached(request.url, request.box, image::ImageKind::Cover)) {
        setCover(ready, context.textures);
        return;
    }
    cancel_ = net::CancelToken();
    const net::CancelToken token = cancel_;
    image::TextureBudget* textures = &context.textures;
    context.images.load(request, token, [this, token, textures](const image::ImageResult& result) {
        if (token.cancelled()) {
            return;  // scrolled away, or the cell is gone
        }
        if (result.ok()) {
            setCover(result.value(), *textures);
        }
    });
}

void CoverCell::clear() {
    cancel_.cancel();
    image_->clear();
    lease_.reset();
}

core::ImageSize CoverCell::coverBox() {
    const float scale = std::max(1.0f, brls::Application::windowScale);
    return {static_cast<int>(kCoverWidth * scale), static_cast<int>(kCoverHeight * scale)};
}

void CoverCell::setCover(const image::ImagePtr& decoded, image::TextureBudget& textures) {
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

void prefetchCovers(app::AppContext& context, const std::vector<core::Manga>& items, std::size_t from, std::size_t count,
                    const net::CancelToken& token) {
    if (!context.profile.coverRowPrefetch) {
        return;
    }
    for (std::size_t i = from; i < std::min(items.size(), from + count); ++i) {
        if (items[i].thumbnailUrl.empty()) {
            continue;
        }
        image::LoadRequest request{context.client.absoluteUrl(items[i].thumbnailUrl), CoverCell::coverBox(),
                                   net::Priority::Prefetch, image::ImageKind::Cover};
        context.images.load(std::move(request), token, [](const image::ImageResult&) {});
    }
}

}  // namespace snx::ui
