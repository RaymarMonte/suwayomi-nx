#include "ui/reader/ReaderActivity.hpp"

#include <utility>

#include "app/TestHooks.hpp"
#include "ui/components/StatusView.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

ReaderActivity::ReaderActivity(app::AppContext& context, core::Manga manga, core::Chapter chapter, int page)
    : context_(context), manga_(std::move(manga)), chapter_(std::move(chapter)), page_(page) {}

brls::View* ReaderActivity::createContentView() {
    (void)context_;  // WP-11 loads the pages through it
    auto* status = new StatusView();
    status->showMessage(chapter_.name, brls::getStr("reader/soon", chapter_.name, page_ + 1));
    auto* frame = new brls::AppletFrame(status);
    frame->setTitle(manga_.title);
    return frame;
}

void ReaderActivity::onContentAvailable() {
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["mangaId"] = manga_.id;
    params["chapterId"] = chapter_.id;
    params["page"] = page_;
    app::testhooks::screen("Reader", params);
}

}  // namespace snx::ui
