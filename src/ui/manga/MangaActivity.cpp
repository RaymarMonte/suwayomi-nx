#include "ui/manga/MangaActivity.hpp"

#include <utility>

#include "app/TestHooks.hpp"
#include "ui/components/StatusView.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

MangaActivity::MangaActivity(app::AppContext& context, core::Manga manga) : context_(context), manga_(std::move(manga)) {}

brls::View* MangaActivity::createContentView() {
    auto* status = new StatusView();
    status->showMessage(manga_.title, "library/manga_soon"_i18n);
    auto* frame = new brls::AppletFrame(status);
    frame->setTitle(manga_.title);
    return frame;
}

void MangaActivity::onContentAvailable() {
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["id"] = manga_.id;
    params["title"] = manga_.title;
    app::testhooks::screen("Manga", params);
}

}  // namespace snx::ui
