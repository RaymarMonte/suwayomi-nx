#pragma once

#include <functional>
#include <string>

// One way to ask the user for a line of text (WP-24): the system keyboard (the Switch's, or borealis's dialog on the PC).
// In test builds a text sent with the test command {"cmd":"text","text":"..."} answers instead (the Switch's keyboard
// can't be typed into by the harness). `done` runs on the main thread after the keyboard closed, only when the user
// confirmed. While the keyboard is open the main loop stands still on purpose (the watchdog knows, WP-12).
namespace snx::ui {

void askText(const std::string& header, const std::string& initial, int maxChars, std::function<void(std::string)> done);

}  // namespace snx::ui
