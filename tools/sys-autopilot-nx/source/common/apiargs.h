#pragma once

#include "json.h"

#include <stdbool.h>
#include <stdint.h>

// Shared JSON argument extraction for input endpoints (REST bodies and MCP
// tool arguments use identical shapes).

// {"buttons":["A","B"], ...} -> combined mask. *err set on failure.
bool args_get_buttons(const JsonDoc *doc, int obj, uint64_t *out_mask,
                      const char **err);

// Optional {"durationMs": N}; returns fallback when absent.
int args_get_duration(const JsonDoc *doc, int obj, int fallback);

// {"side":"left|right","x":F,"y":F,"durationMs":N} -> parsed stick command.
bool args_get_stick(const JsonDoc *doc, int obj, int *out_side, float *out_x,
                    float *out_y, int *out_duration, const char **err);

// --- touch screen (sys-autopilot-nx) -----------------------------------------

// The touch screen's coordinate space: screen pixels, origin top-left.
#define TOUCH_SCREEN_WIDTH   1280
#define TOUCH_SCREEN_HEIGHT  720
// Same caps as the button endpoints (input.c checks they match input.h).
#define ARGS_DEFAULT_TAP_MS  100
#define ARGS_MAX_DURATION_MS 10000
#define TOUCH_MAX_STEPS      1000
// Default swipe resolution: one finger position per ~16 ms (one 60 fps frame).
#define TOUCH_DEFAULT_STEP_MS 16

typedef struct {
    int x, y;        // 0..1279, 0..719
    int duration_ms; // 1..10000 (bigger values are capped at 10000)
} TouchArgs;

typedef struct {
    int x1, y1, x2, y2; // 0..1279, 0..719
    int duration_ms;    // 1..10000 (bigger values are capped at 10000)
    int steps;          // 1..1000
} SwipeArgs;

// {"x":N,"y":N,"durationMs":N?} -> touch (tap, or hold when durationMs is
// long). Rules: integers only (no strings, fractions, booleans); x/y must be
// on screen; durationMs defaults to 100, must be >= 1, values above 10000 are
// capped. On failure returns false with *err set (an HTTP 400 message).
bool args_get_touch(const JsonDoc *doc, int obj, TouchArgs *out, const char **err);

// {"x1","y1","x2","y2","durationMs","steps"?} -> swipe. Same rules; durationMs
// is required here; steps defaults to durationMs/16 (at least 1) and must be
// 1..1000 when given.
bool args_get_swipe(const JsonDoc *doc, int obj, SwipeArgs *out, const char **err);
