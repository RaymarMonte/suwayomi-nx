#include "ui/components/AskText.hpp"

#include <borealis.hpp>
#include <optional>
#include <utility>

#include "app/TestHooks.hpp"
#include "app/crash/Crash.hpp"

namespace snx::ui {

void askText(const std::string& header, const std::string& initial, int maxChars, std::function<void(std::string)> done) {
    if (std::optional<std::string> typed = app::testhooks::takeTypedText()) {
        brls::sync([done = std::move(done), text = std::move(*typed)] { done(text); });
        return;
    }
    const app::crash::BlockingWait typing;
    brls::Application::getImeManager()->openForText(
        [done = std::move(done)](std::string text) { brls::sync([done, text = std::move(text)] { done(text); }); },
        header, "", maxChars, initial);
}

}  // namespace snx::ui
