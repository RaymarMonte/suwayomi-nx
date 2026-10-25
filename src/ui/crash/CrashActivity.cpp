#include "ui/crash/CrashActivity.hpp"

#include <utility>

#include "app/crash/Crash.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {
constexpr long kAutoCloseMs = 60'000;
constexpr int kExitDeadlineMs = 3000;
}  // namespace

CrashActivity::CrashActivity(std::string recordName) : recordName_(std::move(recordName)) {}

void CrashActivity::onContentAvailable() {
    record->setText(recordName_);
    const auto closeAction = [this](brls::View*) {
        close();
        return true;
    };
    // A and B show "Close" in the footer (otherwise the frame's own "Back" hint would show for B); the others just work.
    for (const brls::ControllerButton button :
         {brls::BUTTON_A, brls::BUTTON_B, brls::BUTTON_X, brls::BUTTON_Y, brls::BUTTON_BACK}) {
        const bool hidden = button != brls::BUTTON_A && button != brls::BUTTON_B;
        box->registerAction("crash/close"_i18n, button, closeAction, hidden);
    }
    box->addGestureRecognizer(new brls::TapGestureRecognizer(box, [this] { close(); }));
    brls::delay(kAutoCloseMs, [this] { close(); });
}

void CrashActivity::close() {
    if (closing_) {
        return;
    }
    closing_ = true;
    // The crashed thread is parked and may hold something borealis waits for while it shuts down: if quitting hangs,
    // the watchdog ends the process after this deadline.
    app::crash::armExitDeadline(kExitDeadlineMs);
    brls::Application::quit();
}

}  // namespace snx::ui
