#include "ui/hello/HelloActivity.hpp"

#include "core/Version.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

void HelloActivity::onContentAvailable() {
    version->setText("hello/version"_i18n + " " + core::versionLabel());
}

}  // namespace snx::ui
