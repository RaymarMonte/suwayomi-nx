#include "ui/components/StatusView.hpp"

#include <utility>

#include "app/TestHooks.hpp"
#include "platform/Platform.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {
constexpr float kTouchTarget = 72.0f;  // D16: thumb-sized
}  // namespace

std::string errorText(const api::ApiError& error) {
    if (error.kind == api::ApiErrorKind::Offline && !platform::networkConnected()) {
        return "status/no_network"_i18n;
    }
    return error.message;
}

StatusView::StatusView() {
    setAxis(brls::Axis::COLUMN);
    setJustifyContent(brls::JustifyContent::CENTER);
    setAlignItems(brls::AlignItems::CENTER);
    setGrow(1.0f);
    setPadding(40, 60, 40, 60);
    setFocusable(true);
    setHideHighlight(true);
    setHideClickAnimation(true);

    title_ = new brls::Label();
    title_->setFontSize(26);
    title_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    addView(title_);

    text_ = new brls::Label();
    text_->setFontSize(20);
    text_->setMarginTop(12);
    text_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    addView(text_);

    retry_ = new brls::Button();
    retry_->setText("status/retry"_i18n);
    retry_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    retry_->setMarginTop(24);
    retry_->setWidth(280);
    retry_->setHeight(kTouchTarget);
    retry_->setVisibility(brls::Visibility::GONE);
    retry_->registerClickAction([this](brls::View*) {
        if (onRetry_) {
            // Copy first: the retry may replace onRetry_ (it shows "loading" right away).
            const std::function<void()> retry = onRetry_;
            retry();
        }
        return true;
    });
    addView(retry_);
}

void StatusView::showLoading(const std::string& text) {
    onRetry_ = nullptr;
    title_->setText(text);
    text_->setText("");
    text_->setVisibility(brls::Visibility::GONE);
    if (retry_->isFocused()) {
        brls::Application::giveFocus(this);
    }
    retry_->setVisibility(brls::Visibility::GONE);
}

void StatusView::showError(const std::string& where, const api::ApiError& error, std::function<void()> onRetry) {
    const std::string message = errorText(error);
    brls::Logger::warning("{}: {} ({})", where, message, error.detail);
    app::testhooks::error(where, api::apiErrorKindName(error.kind), message);
    onRetry_ = std::move(onRetry);
    title_->setText(message);
    text_->setVisibility(brls::Visibility::GONE);
    retry_->setVisibility(onRetry_ ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (onRetry_ && isFocused()) {
        brls::Application::giveFocus(retry_);
    }
}

void StatusView::showMessage(const std::string& title, const std::string& text) {
    onRetry_ = nullptr;
    title_->setText(title);
    text_->setText(text);
    text_->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    if (retry_->isFocused()) {
        brls::Application::giveFocus(this);
    }
    retry_->setVisibility(brls::Visibility::GONE);
}

brls::View* StatusView::getDefaultFocus() {
    if (retry_->getVisibility() == brls::Visibility::VISIBLE) {
        return retry_;
    }
    return this;
}

TextFieldCell::TextFieldCell() {
    detail->setTextColor(brls::Application::getTheme()["brls/list/listItem_value_color"]);
    setMinHeight(kTouchTarget);
    registerClickAction([this](brls::View*) {
        // The keyboard closes before the callback runs, on the main thread (swkbd blocks the frame on the Switch).
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                setValue(text);
                if (onChange_) {
                    onChange_(value_);
                }
            },
            title->getFullText(), hint_, maxLength_, secret_ ? std::string() : value_);
        return true;
    });
    showValue();
}

brls::View* TextFieldCell::create() {
    return new TextFieldCell();
}

void TextFieldCell::setup(std::string value, bool secret, int maxLength, std::string hint,
                          std::function<void(std::string)> onChange) {
    value_ = std::move(value);
    secret_ = secret;
    maxLength_ = maxLength;
    hint_ = std::move(hint);
    onChange_ = std::move(onChange);
    showValue();
}

void TextFieldCell::setValue(std::string value) {
    value_ = std::move(value);
    showValue();
}

void TextFieldCell::showValue() {
    if (value_.empty()) {
        setDetailText("field/tap_to_enter"_i18n);
    } else if (secret_) {
        std::string dots;
        for (std::size_t i = 0; i < value_.size() && i < 16; ++i) {
            dots += "\xE2\x80\xA2";  // a bullet
        }
        setDetailText(dots);
    } else {
        setDetailText(value_);
    }
}

}  // namespace snx::ui
