// Host tests for the touch/swipe argument parsing (sys-autopilot-nx).
// Each case is one JSON body as the REST endpoint or MCP tool receives it.
// To add cases, add rows to the tables below.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "apiargs.h"
#include "json.h"

static JsonDoc g_doc;

typedef struct {
    const char *json;
    bool ok;
    // expected values when ok
    int x, y, duration;
    // expected substring of the error message when !ok
    const char *err;
} TouchCase;

static const TouchCase kTouchCases[] = {
    // valid
    { "{\"x\":640,\"y\":360}",                      true,  640, 360, 100, NULL },
    { "{\"x\":0,\"y\":0}",                          true,  0,   0,   100, NULL },
    { "{\"x\":1279,\"y\":719}",                     true,  1279, 719, 100, NULL },
    { "{\"x\":10,\"y\":20,\"durationMs\":1}",       true,  10,  20,  1,   NULL },
    { "{\"x\":10,\"y\":20,\"durationMs\":2500}",    true,  10,  20,  2500, NULL },
    { "{\"x\":10,\"y\":20,\"durationMs\":10000}",   true,  10,  20,  10000, NULL },
    { "{\"x\":10,\"y\":20,\"durationMs\":10001}",   true,  10,  20,  10000, NULL }, // capped
    { "{\"durationMs\":50,\"y\":5,\"x\":7}",        true,  7,   5,   50,  NULL },   // any key order
    { "{\"x\":-0,\"y\":3}",                         true,  0,   3,   100, NULL },
    { "{\"x\":5,\"y\":5,\"extra\":\"ignored\"}",    true,  5,   5,   100, NULL },
    // missing fields
    { "{}",                                         false, 0, 0, 0, "missing 'x'" },
    { "{\"x\":5}",                                  false, 0, 0, 0, "missing 'y'" },
    { "{\"y\":5}",                                  false, 0, 0, 0, "missing 'x'" },
    { "{\"X\":5,\"Y\":5}",                          false, 0, 0, 0, "missing 'x'" }, // keys are case-sensitive
    // out of range
    { "{\"x\":-1,\"y\":5}",                         false, 0, 0, 0, "'x' must be between 0 and 1279" },
    { "{\"x\":1280,\"y\":5}",                       false, 0, 0, 0, "'x' must be between 0 and 1279" },
    { "{\"x\":5,\"y\":720}",                        false, 0, 0, 0, "'y' must be between 0 and 719" },
    { "{\"x\":5,\"y\":-719}",                       false, 0, 0, 0, "'y' must be between 0 and 719" },
    { "{\"x\":5,\"y\":5,\"durationMs\":0}",         false, 0, 0, 0, "'durationMs' must be at least 1" },
    { "{\"x\":5,\"y\":5,\"durationMs\":-100}",      false, 0, 0, 0, "'durationMs' must be at least 1" },
    // wrong types
    { "{\"x\":\"5\",\"y\":5}",                      false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":5.5,\"y\":5}",                        false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":5.0,\"y\":5}",                        false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":1e2,\"y\":5}",                        false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":true,\"y\":5}",                       false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":null,\"y\":5}",                       false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":[5],\"y\":5}",                        false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":{\"v\":5},\"y\":5}",                  false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":5,\"y\":\"\"}",                       false, 0, 0, 0, "'y' must be an integer" },
    { "{\"x\":-,\"y\":5}",                          false, 0, 0, 0, "'x' must be an integer" },
    { "{\"x\":5,\"y\":5,\"durationMs\":\"100\"}",   false, 0, 0, 0, "'durationMs' must be an integer" },
    { "{\"x\":5,\"y\":5,\"durationMs\":1.5}",       false, 0, 0, 0, "'durationMs' must be an integer" },
    // huge numbers: no overflow, coordinates rejected, durations capped
    { "{\"x\":99999999999999999999999999,\"y\":5}", false, 0, 0, 0, "'x' must be between 0 and 1279" },
    { "{\"x\":-99999999999999999999999999,\"y\":5}",false, 0, 0, 0, "'x' must be between 0 and 1279" },
    { "{\"x\":4294967296,\"y\":5}",                 false, 0, 0, 0, "'x' must be between 0 and 1279" }, // 2^32 must not wrap to 0
    { "{\"x\":4294967936,\"y\":5}",                 false, 0, 0, 0, "'x' must be between 0 and 1279" }, // 2^32+640
    { "{\"x\":5,\"y\":5,\"durationMs\":99999999999999999999}", true, 5, 5, 10000, NULL },
    { "{\"x\":5,\"y\":5,\"durationMs\":4294967297}", true, 5, 5, 10000, NULL },      // 2^32+1 must not wrap to 1
    { "{\"x\":5,\"y\":5,\"durationMs\":-99999999999999999999}", false, 0, 0, 0, "'durationMs' must be at least 1" },
};

typedef struct {
    const char *json;
    bool ok;
    int x1, y1, x2, y2, duration, steps;
    const char *err;
} SwipeCase;

static const SwipeCase kSwipeCases[] = {
    // valid
    { "{\"x1\":640,\"y1\":600,\"x2\":640,\"y2\":100,\"durationMs\":300}",
      true, 640, 600, 640, 100, 300, 18, NULL }, // default steps = 300/16
    { "{\"x1\":0,\"y1\":0,\"x2\":1279,\"y2\":719,\"durationMs\":500,\"steps\":5}",
      true, 0, 0, 1279, 719, 500, 5, NULL },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":10}",
      true, 1, 2, 3, 4, 10, 1, NULL },           // very short: at least 1 step
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":50000}",
      true, 1, 2, 3, 4, 10000, 625, NULL },      // capped, then default steps
    { "{\"x1\":1,\"y1\":2,\"x2\":1,\"y2\":2,\"durationMs\":100,\"steps\":1}",
      true, 1, 2, 1, 2, 100, 1, NULL },          // same point = press and hold
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100,\"steps\":1000}",
      true, 1, 2, 3, 4, 100, 1000, NULL },
    // missing fields
    { "{}", false, 0, 0, 0, 0, 0, 0, "missing 'x1'" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4}", false, 0, 0, 0, 0, 0, 0, "missing 'durationMs'" },
    { "{\"x1\":1,\"y1\":2,\"y2\":4,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "missing 'x2'" },
    { "{\"x1\":1,\"x2\":3,\"y2\":4,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "missing 'y1'" },
    { "{\"x\":1,\"y\":2,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "missing 'x1'" }, // touch shape is not a swipe
    // out of range
    { "{\"x1\":1,\"y1\":2,\"x2\":1280,\"y2\":4,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "'x2' must be between 0 and 1279" },
    { "{\"x1\":1,\"y1\":-1,\"x2\":3,\"y2\":4,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "'y1' must be between 0 and 719" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":720,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "'y2' must be between 0 and 719" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":0}", false, 0, 0, 0, 0, 0, 0, "'durationMs' must be at least 1" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100,\"steps\":0}", false, 0, 0, 0, 0, 0, 0, "'steps' must be between 1 and 1000" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100,\"steps\":1001}", false, 0, 0, 0, 0, 0, 0, "'steps' must be between 1 and 1000" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100,\"steps\":-3}", false, 0, 0, 0, 0, 0, 0, "'steps' must be between 1 and 1000" },
    // wrong types
    { "{\"x1\":\"1\",\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "'x1' must be an integer" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3.5,\"y2\":4,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "'x2' must be an integer" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":false}", false, 0, 0, 0, 0, 0, 0, "'durationMs' must be an integer" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100,\"steps\":\"5\"}", false, 0, 0, 0, 0, 0, 0, "'steps' must be an integer" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100,\"steps\":null}", false, 0, 0, 0, 0, 0, 0, "'steps' must be an integer" },
    // huge numbers
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":100,\"steps\":99999999999999999999}", false, 0, 0, 0, 0, 0, 0, "'steps' must be between 1 and 1000" },
    { "{\"x1\":1,\"y1\":99999999999,\"x2\":3,\"y2\":4,\"durationMs\":100}", false, 0, 0, 0, 0, 0, 0, "'y1' must be between 0 and 719" },
    { "{\"x1\":1,\"y1\":2,\"x2\":3,\"y2\":4,\"durationMs\":99999999999999999999}",
      true, 1, 2, 3, 4, 10000, 625, NULL },
};

static void parse(const char *json) {
    int rc = json_parse(&g_doc, json, strlen(json));
    if (rc != 0 || g_doc.ntok < 1) {
        fprintf(stderr, "test JSON does not parse: %s\n", json);
        assert(0);
    }
}

static void fail(const char *json, const char *what, const char *got) {
    fprintf(stderr, "FAILED: %s\n  case: %s\n  got: %s\n", what, json, got ? got : "(null)");
    assert(0);
}

static void test_touch_cases(void) {
    for (size_t i = 0; i < sizeof(kTouchCases) / sizeof(kTouchCases[0]); i++) {
        const TouchCase *c = &kTouchCases[i];
        parse(c->json);
        TouchArgs a;
        memset(&a, 0xAB, sizeof(a));
        const char *err = NULL;
        bool ok = args_get_touch(&g_doc, 0, &a, &err);
        if (ok != c->ok)
            fail(c->json, c->ok ? "expected success" : "expected an error", err);
        if (ok) {
            if (a.x != c->x || a.y != c->y || a.duration_ms != c->duration)
                fail(c->json, "wrong values", NULL);
        } else if (!err || !strstr(err, c->err)) {
            fail(c->json, c->err, err);
        }
    }
    printf("touch parsing ok (%zu cases)\n", sizeof(kTouchCases) / sizeof(kTouchCases[0]));
}

static void test_swipe_cases(void) {
    for (size_t i = 0; i < sizeof(kSwipeCases) / sizeof(kSwipeCases[0]); i++) {
        const SwipeCase *c = &kSwipeCases[i];
        parse(c->json);
        SwipeArgs a;
        memset(&a, 0xAB, sizeof(a));
        const char *err = NULL;
        bool ok = args_get_swipe(&g_doc, 0, &a, &err);
        if (ok != c->ok)
            fail(c->json, c->ok ? "expected success" : "expected an error", err);
        if (ok) {
            if (a.x1 != c->x1 || a.y1 != c->y1 || a.x2 != c->x2 || a.y2 != c->y2 ||
                a.duration_ms != c->duration || a.steps != c->steps)
                fail(c->json, "wrong values", NULL);
        } else if (!err || !strstr(err, c->err)) {
            fail(c->json, c->err, err);
        }
    }
    printf("swipe parsing ok (%zu cases)\n", sizeof(kSwipeCases) / sizeof(kSwipeCases[0]));
}

// MCP calls a tool without "arguments" as obj = -1: must be a clean
// "missing" error, never a crash.
static void test_no_arguments_object(void) {
    parse("{}");
    TouchArgs t;
    SwipeArgs s;
    const char *err = NULL;
    assert(!args_get_touch(&g_doc, -1, &t, &err) && strstr(err, "missing 'x'"));
    err = NULL;
    assert(!args_get_swipe(&g_doc, -1, &s, &err) && strstr(err, "missing 'x1'"));
    // The body itself is not an object (e.g. an array).
    parse("[1,2]");
    err = NULL;
    assert(!args_get_touch(&g_doc, 0, &t, &err) && strstr(err, "missing 'x'"));
    printf("no-arguments ok\n");
}

int main(void) {
    test_touch_cases();
    test_swipe_cases();
    test_no_arguments_object();
    printf("all touch tests passed\n");
    return 0;
}
