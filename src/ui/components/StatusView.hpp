#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

#include "api/ApiError.hpp"

// The three states every screen has besides its content (WP-08), reused by all screens: loading ("Connecting..."), an error
// with a Retry button, and a plain message (empty or "coming later"). Built in code, centred in the space it gets. No
// spinner: a running animation would keep the app from ever counting as idle in tests, and the text says enough.
namespace snx::ui {

// The sentence a screen shows for an api error: its message, or "the Switch isn't connected to a network" when it is
// offline because the console has no network at all (platform::networkConnected()).
std::string errorText(const api::ApiError& error);

class StatusView : public brls::Box {
  public:
    StatusView();

    void showLoading(const std::string& text);
    // Writes an "error" test event naming `where` (the screen). Without onRetry there is no Retry button.
    void showError(const std::string& where, const api::ApiError& error, std::function<void()> onRetry);
    void showMessage(const std::string& title, const std::string& text);

    // The Retry button while an error shows, else this view itself (focusable, highlight hidden, so buttons always work).
    brls::View* getDefaultFocus() override;

  private:
    brls::Label* title_;
    brls::Label* text_;
    brls::Button* retry_;
    std::function<void()> onRetry_;
};

// A text field (WP-08, touch first D16): a cell with a title and the current value; a tap or A opens the keyboard (the Switch's
// swkbd, the desktop's text dialog). Secret fields show dots. XML name: <TextFieldCell id=... title=.../>.
class TextFieldCell : public brls::DetailCell {
  public:
    TextFieldCell();
    static brls::View* create();

    // `onChange` gets the new text after the keyboard closes with OK.
    void setup(std::string value, bool secret, int maxLength, std::string hint, std::function<void(std::string)> onChange);
    void setValue(std::string value);
    const std::string& value() const { return value_; }

  private:
    void showValue();

    std::string value_;
    std::string hint_;
    bool secret_ = false;
    int maxLength_ = 256;
    std::function<void(std::string)> onChange_;
};

}  // namespace snx::ui
