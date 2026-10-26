#pragma once

#include "core/RuntimeInfo.hpp"
#include "platform/SafeFs.hpp"

// The applet-mode guard (D21, SAFETY.md S12). The app runs only in application mode ("hold R on a game"). Opened from the
// Album it is in applet mode, where any crash is a system fatal, so this path stays tiny: it runs right after the crash
// handler and the logger are set up, before borealis, the GPU, network, images or config.
namespace snx::app {

// Application mode: returns false at once and the app starts normally. Applet mode (unknown applet types too): logs one line,
// writes the `launch_refused` test event (and, in test builds, the crash drill's `drill` event when the test config names
// "null-write"), makes sure both reached the SD card, then shows D21's text until a button, a tap or 60 s (the drill runs
// inside the screen), and returns true. main() then returns at once and writes nothing more. If the screen can't be set up
// it is skipped and true is returned all the same.
bool refuseAppletMode(const core::RuntimeInfo& runtime, const platform::SafeFs& fs);

// D21's text, owner-approved 2026-10-25, wrapped for libnx's 80-column text console.
extern const char* const kAppletModeText;

}  // namespace snx::app
