#pragma once

#include <switch.h>

#define INPUT_DEFAULT_TAP_MS 100
#define INPUT_MAX_DURATION_MS 10000

// Detaches the virtual device and releases the HDLS work buffer. MUST be
// called when the console prepares for sleep (holding the work buffer
// across a sleep transition crashes the console); everything re-attaches
// lazily on the next input request.
void input_suspend(void);

// Alias of input_suspend for shutdown paths. Call before hiddbgExit.
void input_exit(void);

// Attach/detach the virtual Pro Controller.
Result input_attach(void);
Result input_detach(void);
bool   input_is_attached(void);

// Press+release: applies mask, sleeps duration_ms, releases mask.
Result input_tap(u64 mask, int duration_ms);

Result input_hold(u64 mask);
Result input_release(u64 mask);
Result input_clear(void);

// side: 0 = left stick, 1 = right stick. x/y in [-1.0, 1.0].
// If duration_ms > 0, the stick returns to neutral afterwards.
Result input_stick(int side, float x, float y, int duration_ms);

// --- touch screen (sys-autopilot-nx) -----------------------------------------
// Coordinates are screen pixels (x 0-1279, y 0-719), already validated by
// apiargs.c. While our finger is down, hid ignores real touches, so every
// call below lifts the finger and hands the touch panel back before it
// returns, even when a step in between failed.

// One finger down at x,y for duration_ms (tap or press-and-hold), then lifted.
Result input_touch(int x, int y, int duration_ms);

// One finger dragged from x1,y1 to x2,y2 in `steps` equal moves spread over
// duration_ms, then lifted.
Result input_swipe(int x1, int y1, int x2, int y2, int duration_ms, int steps);
