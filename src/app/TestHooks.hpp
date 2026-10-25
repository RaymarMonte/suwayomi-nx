#pragma once

#include <string>
#include <string_view>

#include "core/RuntimeInfo.hpp"

// Test events (docs/TESTING.md "Test hooks inside the app"). Test builds (SNX_TEST_HOOKS) write one JSON line per event to
// <data>/test/events.jsonl, and the E2E harness tails that file. Release builds compile all of this to nothing.
// WP-03 has app_start, screen and idle. The full set (focus, net, image, reader, mem, error, ...) comes in WP-08.
// Main thread only for now.
namespace snx::app::testhooks {

// Empties the events file and starts the clock that every event's "t" (ms) counts from. Call once, first.
void start(const std::string& dataDir);

// {"ev":"app_start", version, build, platform, mode, profile, memTotal, memUsed}
void appStart(const core::RuntimeInfo& runtime, std::string_view profile);

// {"ev":"screen","name":...}: a screen is now on top. It also makes the next quiet spell report "idle" again.
void screen(std::string_view name);

// Once per main-loop iteration: writes "idle" when no animation has run for a few frames, and retries lines that couldn't be
// written yet (on the Switch the file is busy while the harness reads it).
void onFrame();

// At exit: writes what is still pending (tries for about 100 ms).
void finish();

}  // namespace snx::app::testhooks
