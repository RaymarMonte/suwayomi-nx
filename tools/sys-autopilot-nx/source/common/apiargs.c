#include "apiargs.h"
#include "buttons.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

bool args_get_buttons(const JsonDoc *doc, int obj, uint64_t *out_mask,
                      const char **err) {
    int arr = json_obj_get(doc, obj, "buttons");
    if (arr < 0) {
        *err = "missing 'buttons' (array of button names)";
        return false;
    }
    int n = json_arr_len(doc, arr);
    if (n <= 0) {
        *err = "'buttons' must be a non-empty array";
        return false;
    }
    uint64_t mask = 0;
    for (int i = 0; i < n; i++) {
        int tok = json_arr_get(doc, arr, i);
        char name[24];
        uint64_t m;
        if (!json_get_string(doc, tok, name, sizeof(name)) ||
            !button_from_name(name, &m)) {
            *err = "unknown button name in 'buttons'";
            return false;
        }
        mask |= m;
    }
    *out_mask = mask;
    return true;
}

int args_get_duration(const JsonDoc *doc, int obj, int fallback) {
    long long v;
    int tok = json_obj_get(doc, obj, "durationMs");
    if (tok >= 0 && json_get_int(doc, tok, &v))
        return (int)v;
    return fallback;
}

bool args_get_stick(const JsonDoc *doc, int obj, int *out_side, float *out_x,
                    float *out_y, int *out_duration, const char **err) {
    char side[16] = "";
    int tok = json_obj_get(doc, obj, "side");
    if (tok < 0 || !json_get_string(doc, tok, side, sizeof(side))) {
        *err = "missing 'side' (\"left\" or \"right\")";
        return false;
    }
    if (strcasecmp(side, "left") == 0)
        *out_side = 0;
    else if (strcasecmp(side, "right") == 0)
        *out_side = 1;
    else {
        *err = "invalid 'side' (\"left\" or \"right\")";
        return false;
    }

    double x = 0.0, y = 0.0;
    tok = json_obj_get(doc, obj, "x");
    if (tok >= 0 && !json_get_double(doc, tok, &x)) {
        *err = "'x' must be a number";
        return false;
    }
    tok = json_obj_get(doc, obj, "y");
    if (tok >= 0 && !json_get_double(doc, tok, &y)) {
        *err = "'y' must be a number";
        return false;
    }
    *out_x = (float)x;
    *out_y = (float)y;
    *out_duration = args_get_duration(doc, obj, 0);
    return true;
}

// --- touch screen (sys-autopilot-nx) -----------------------------------------

// Error text for the last failed touch/swipe parse (the server handles one
// request at a time, so one static buffer is enough).
static char g_touch_err[96];

typedef enum { INT_OK, INT_MISSING, INT_NOT_INT } IntField;

// Reads obj[key] as a plain JSON integer: optional '-', then digits only.
// Strings, fractions, exponents, booleans, null, arrays and objects are
// rejected. Numbers beyond +-1,000,000,000 are saturated to that value, which
// is outside every range we accept (so huge numbers fail the range check, and
// huge durations get capped), without any overflow.
static IntField get_int_field(const JsonDoc *doc, int obj, const char *key,
                              long long *out) {
    int tok = json_obj_get(doc, obj, key);
    if (tok < 0)
        return INT_MISSING;
    if (doc->tok[tok].type != JSMN_PRIMITIVE)
        return INT_NOT_INT;
    const char *p = doc->src + doc->tok[tok].start;
    int len = doc->tok[tok].end - doc->tok[tok].start;
    int i = 0;
    bool negative = false;
    if (i < len && p[i] == '-') {
        negative = true;
        i++;
    }
    if (i == len)
        return INT_NOT_INT;
    long long v = 0;
    for (; i < len; i++) {
        if (p[i] < '0' || p[i] > '9')
            return INT_NOT_INT;
        if (v < 1000000000LL)
            v = v * 10 + (p[i] - '0');
    }
    if (v > 1000000000LL)
        v = 1000000000LL;
    *out = negative ? -v : v;
    return INT_OK;
}

// A required integer in [lo, hi]. On failure sets *err and returns false.
static bool get_int_in_range(const JsonDoc *doc, int obj, const char *key,
                             int lo, int hi, int *out, const char **err) {
    long long v = 0;
    switch (get_int_field(doc, obj, key, &v)) {
        case INT_MISSING:
            snprintf(g_touch_err, sizeof(g_touch_err), "missing '%s'", key);
            *err = g_touch_err;
            return false;
        case INT_NOT_INT:
            snprintf(g_touch_err, sizeof(g_touch_err), "'%s' must be an integer", key);
            *err = g_touch_err;
            return false;
        case INT_OK:
            break;
    }
    if (v < lo || v > hi) {
        snprintf(g_touch_err, sizeof(g_touch_err),
                 "'%s' must be between %d and %d", key, lo, hi);
        *err = g_touch_err;
        return false;
    }
    *out = (int)v;
    return true;
}

static bool get_x(const JsonDoc *doc, int obj, const char *key, int *out,
                  const char **err) {
    return get_int_in_range(doc, obj, key, 0, TOUCH_SCREEN_WIDTH - 1, out, err);
}

static bool get_y(const JsonDoc *doc, int obj, const char *key, int *out,
                  const char **err) {
    return get_int_in_range(doc, obj, key, 0, TOUCH_SCREEN_HEIGHT - 1, out, err);
}

// durationMs: >= 1, capped at ARGS_MAX_DURATION_MS. When absent, uses
// fallback, or fails if fallback is 0 (required).
static bool get_duration(const JsonDoc *doc, int obj, int fallback, int *out,
                         const char **err) {
    long long v = 0;
    switch (get_int_field(doc, obj, "durationMs", &v)) {
        case INT_MISSING:
            if (fallback <= 0) {
                *err = "missing 'durationMs'";
                return false;
            }
            *out = fallback;
            return true;
        case INT_NOT_INT:
            *err = "'durationMs' must be an integer";
            return false;
        case INT_OK:
            break;
    }
    if (v < 1) {
        *err = "'durationMs' must be at least 1";
        return false;
    }
    *out = v > ARGS_MAX_DURATION_MS ? ARGS_MAX_DURATION_MS : (int)v;
    return true;
}

bool args_get_touch(const JsonDoc *doc, int obj, TouchArgs *out, const char **err) {
    TouchArgs a;
    if (!get_x(doc, obj, "x", &a.x, err) ||
        !get_y(doc, obj, "y", &a.y, err) ||
        !get_duration(doc, obj, ARGS_DEFAULT_TAP_MS, &a.duration_ms, err))
        return false;
    *out = a;
    return true;
}

bool args_get_swipe(const JsonDoc *doc, int obj, SwipeArgs *out, const char **err) {
    SwipeArgs a;
    if (!get_x(doc, obj, "x1", &a.x1, err) ||
        !get_y(doc, obj, "y1", &a.y1, err) ||
        !get_x(doc, obj, "x2", &a.x2, err) ||
        !get_y(doc, obj, "y2", &a.y2, err) ||
        !get_duration(doc, obj, 0, &a.duration_ms, err))
        return false;

    if (json_obj_get(doc, obj, "steps") >= 0) {
        if (!get_int_in_range(doc, obj, "steps", 1, TOUCH_MAX_STEPS, &a.steps, err))
            return false;
    } else {
        a.steps = a.duration_ms / TOUCH_DEFAULT_STEP_MS;
        if (a.steps < 1)
            a.steps = 1;
    }
    *out = a;
    return true;
}
