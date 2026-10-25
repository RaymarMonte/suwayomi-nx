#include "routes.h"
#include "apiargs.h"
#include "battery.h"
#include "files.h"
#include "input.h"
#include "json.h"
#include "mcp.h"
#include "screen.h"
#include "log.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

// Injected by the Makefile.
#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

// Cap for JSON request bodies on the REST input endpoints.
#define REST_BODY_MAX 16384

// One parsed request body shared by all REST handlers (the server handles one
// request at a time). Upstream gave every handler its own 16 KiB copy.
static JsonDoc g_doc;

static u64 g_boot_tick;

__attribute__((constructor)) static void init_boot_tick(void) {
    g_boot_tick = armGetSystemTick();
}

const char *routes_app_version(void) {
    return APP_VERSION;
}

u64 routes_uptime_seconds(void) {
    return armTicksToNs(armGetSystemTick() - g_boot_tick) / 1000000000ULL;
}

// Reads and parses a JSON request body into *doc. An absent/empty body parses
// as an empty object. Returns the root token index, or -1 after sending an
// error response.
static int read_json_body(HttpRequest *req, JsonDoc *doc) {
    static char body[REST_BODY_MAX + 1];
    size_t total = 0;

    if (req->has_content_length && req->content_length > REST_BODY_MAX) {
        http_send_error(req->fd, 413, "request body too large");
        return -1;
    }
    ssize_t n;
    while (total < REST_BODY_MAX &&
           (n = http_read_body(req, body + total, REST_BODY_MAX - total)) > 0)
        total += (size_t)n;

    if (total == 0) {
        strcpy(body, "{}");
        total = 2;
    }
    if (json_parse(doc, body, total) != 0 || doc->ntok < 1 ||
        doc->tok[0].type != JSMN_OBJECT) {
        http_send_error(req->fd, 400, "request body must be a JSON object");
        return -1;
    }
    return 0; // root token
}

// --- /screenshot -------------------------------------------------------------

static void handle_screenshot(HttpRequest *req) {
    if (!screen_available()) {
        http_send_error(req->fd, 503, "screenshots unavailable (caps:sc failed to open at boot)");
        return;
    }
    ViLayerStack stack = ViLayerStack_Screenshot;
    char val[32];
    if (http_query_get(req, "stack", val, sizeof(val))) {
        if (!screen_parse_stack(val, &stack)) {
            http_send_error(req->fd, 400, "invalid 'stack' (use screenshot|default|lcd|recording|lastframe)");
            return;
        }
    }

    const u8 *jpeg = NULL;
    u64 size = 0;
    Result rc = screen_capture_jpeg(stack, &jpeg, &size);
    if (R_FAILED(rc)) {
        http_send_json(req->fd, 500, "{\"error\":\"capture failed\",\"rc\":\"0x%x\"}", rc);
        return;
    }
    http_send_response(req->fd, 200, "image/jpeg", jpeg, (size_t)size);
}

// --- /input/* ----------------------------------------------------------------

static void send_input_result(HttpRequest *req, Result rc) {
    if (R_FAILED(rc))
        http_send_json(req->fd, 500, "{\"error\":\"input failed\",\"rc\":\"0x%x\"}", rc);
    else
        http_send_json(req->fd, 200, "{\"ok\":true}");
}

static void handle_input_tap(HttpRequest *req) {
    int root = read_json_body(req, &g_doc);
    if (root < 0)
        return;
    u64 mask;
    const char *err = NULL;
    if (!args_get_buttons(&g_doc, root, &mask, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_tap(mask, args_get_duration(&g_doc, root,
                                                             INPUT_DEFAULT_TAP_MS)));
}

static void handle_input_hold_release(HttpRequest *req, bool hold) {
    int root = read_json_body(req, &g_doc);
    if (root < 0)
        return;
    u64 mask;
    const char *err = NULL;
    if (!args_get_buttons(&g_doc, root, &mask, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, hold ? input_hold(mask) : input_release(mask));
}

static void handle_input_hold(HttpRequest *req)    { handle_input_hold_release(req, true); }
static void handle_input_release(HttpRequest *req) { handle_input_hold_release(req, false); }

static void handle_input_stick(HttpRequest *req) {
    int root = read_json_body(req, &g_doc);
    if (root < 0)
        return;
    int side, duration;
    float x, y;
    const char *err = NULL;
    if (!args_get_stick(&g_doc, root, &side, &x, &y, &duration, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_stick(side, x, y, duration));
}

// POST /input/touch {"x","y","durationMs"?} (sys-autopilot-nx)
static void handle_input_touch(HttpRequest *req) {
    int root = read_json_body(req, &g_doc);
    if (root < 0)
        return;
    TouchArgs a;
    const char *err = NULL;
    if (!args_get_touch(&g_doc, root, &a, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_touch(a.x, a.y, a.duration_ms));
}

// POST /input/swipe {"x1","y1","x2","y2","durationMs","steps"?} (sys-autopilot-nx)
static void handle_input_swipe(HttpRequest *req) {
    int root = read_json_body(req, &g_doc);
    if (root < 0)
        return;
    SwipeArgs a;
    const char *err = NULL;
    if (!args_get_swipe(&g_doc, root, &a, &err)) {
        http_send_error(req->fd, 400, err);
        return;
    }
    send_input_result(req, input_swipe(a.x1, a.y1, a.x2, a.y2, a.duration_ms, a.steps));
}

// --- /status -----------------------------------------------------------------

// Heap numbers for /status (sys-autopilot-nx), to check the slim heap size on
// the console: size = the whole inner heap, peak = the most of it malloc has
// ever claimed, used = in use right now.
// newlib has no real high-water field (mallinfo().usmblks is always 0), so
// peak is mallinfo().arena, the heap space malloc took so far. That only works
// as a peak because main() turns off trimming (M_TRIM_THRESHOLD), so arena
// never shrinks. (In the dev .nro, which doesn't do that, it can shrink.)
static void heap_stats(unsigned *size, unsigned *peak, unsigned *used) {
    extern void *fake_heap_start;
    extern void *fake_heap_end;
    struct mallinfo mi = mallinfo();
    *size = (unsigned)((char *)fake_heap_end - (char *)fake_heap_start);
    *peak = (unsigned)mi.arena;
    *used = (unsigned)mi.uordblks;
}

static void handle_status(HttpRequest *req) {
    u32 ver = hosversionGet();
    uint32_t pct = 0;
    bool charging = false;
    bool have_batt = battery_get(&pct, &charging);
    char batt[64] = "";
    if (have_batt)
        snprintf(batt, sizeof(batt), ",\"batteryPercent\":%u,\"charging\":%s",
                 pct, charging ? "true" : "false");
    unsigned heap_size, heap_peak, heap_used;
    heap_stats(&heap_size, &heap_peak, &heap_used);
    http_send_json(req->fd, 200,
                   "{\"version\":\"" APP_VERSION "\","
                   "\"firmware\":\"%u.%u.%u\","
                   "\"controllerAttached\":%s,"
                   "\"uptimeSeconds\":%llu%s,"
                   "\"screenshots\":%s,"
                   "\"heap\":{\"size\":%u,\"peak\":%u,\"used\":%u}}",
                   HOSVER_MAJOR(ver), HOSVER_MINOR(ver), HOSVER_MICRO(ver),
                   input_is_attached() ? "true" : "false",
                   (unsigned long long)routes_uptime_seconds(), batt,
                   screen_available() ? "true" : "false",
                   heap_size, heap_peak, heap_used);
}

// --- dispatch ----------------------------------------------------------------

typedef struct {
    const char *method;
    const char *path;
    void (*handler)(HttpRequest *req);
} Route;

static void handle_controller_attach(HttpRequest *req) { send_input_result(req, input_attach()); }
static void handle_controller_detach(HttpRequest *req) { send_input_result(req, input_detach()); }
static void handle_input_clear(HttpRequest *req)       { send_input_result(req, input_clear()); }

// sys-autopilot-nx: upstream's /install, /titles, /network/dns, /power/*,
// /settings/* and /oauth/* routes are gone on purpose (they answer 404).
static const Route kRoutes[] = {
    { "GET",    "/screenshot",        handle_screenshot },
    { "GET",    "/status",            handle_status },
    { "POST",   "/input/tap",         handle_input_tap },
    { "POST",   "/input/hold",        handle_input_hold },
    { "POST",   "/input/release",     handle_input_release },
    { "POST",   "/input/stick",       handle_input_stick },
    { "POST",   "/input/touch",       handle_input_touch },
    { "POST",   "/input/swipe",       handle_input_swipe },
    { "POST",   "/input/clear",       handle_input_clear },
    { "POST",   "/controller/attach", handle_controller_attach },
    { "POST",   "/controller/detach", handle_controller_detach },
    { "GET",    "/files",             files_handle_get },
    { "GET",    "/files/hash",        files_handle_hash },
    { "PUT",    "/files",             files_handle_put },
    { "DELETE", "/files",             files_handle_delete },
    { "POST",   "/mcp",               mcp_handle_post },
    { "GET",    "/mcp",               mcp_handle_get },
};

// CORS preflight: browsers send OPTIONS with no Authorization header before
// cross-origin requests (e.g. web-based MCP clients).
static void handle_options(HttpRequest *req) {
    static const char hdr[] =
        "HTTP/1.1 204 No Content\r\n"
        "Server: sys-autopilot\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Authorization, Content-Type, Mcp-Protocol-Version\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Connection: close\r\n"
        "\r\n";
    http_write_all(req->fd, hdr, sizeof(hdr) - 1);
}

void routes_handle(HttpRequest *req) {
    if (strcmp(req->method, "OPTIONS") == 0) {
        handle_options(req);
        return;
    }

    bool path_found = false;
    for (size_t i = 0; i < sizeof(kRoutes) / sizeof(kRoutes[0]); i++) {
        if (strcmp(req->path, kRoutes[i].path) != 0)
            continue;
        path_found = true;
        if (strcmp(req->method, kRoutes[i].method) == 0) {
            kRoutes[i].handler(req);
            return;
        }
    }
    if (path_found)
        http_send_error(req->fd, 405, "method not allowed");
    else
        http_send_error(req->fd, 404, "not found");
}
