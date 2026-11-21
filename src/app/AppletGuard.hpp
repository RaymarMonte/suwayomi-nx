#pragma once

#include "core/RuntimeInfo.hpp"
#include "core/RuntimeProfile.hpp"
#include "platform/SafeFs.hpp"

// The applet-mode guard (SAFETY.md S12; D21, amended by D62 in WP-21). Opened from the Album the app runs with the Lite
// profile, unless there is too little free memory at start (a game open behind the Album takes some): then this tiny screen
// says so and the app leaves. It runs right after the crash handler and the logger are set up, before borealis, the GPU,
// network, images or config, so a crash can't come from anything heavy.
namespace snx::app {

// Application mode, or applet mode with enough free memory (core::tooLittleMemoryForLite): returns false at once and the app
// starts normally. Otherwise: logs one line, writes the `launch_refused` test event with reason "low_memory" (and, in test
// builds, the crash drill's `drill` event when the test config names "null-write"), makes sure both reached the SD card,
// then shows kLowMemoryText until a button, a tap, an exit request or 60 s (the drill runs inside the screen), and returns
// true. main() then returns at once and writes nothing more. If the screen can't be set up it is skipped and true is
// returned all the same.
bool refuseLowMemory(const core::RuntimeInfo& runtime, const core::RuntimeProfile& profile, const platform::SafeFs& fs);

// The low-memory screen's text (WP-21, D62), wrapped for libnx's 80-column text console.
extern const char* const kLowMemoryText;

}  // namespace snx::app
