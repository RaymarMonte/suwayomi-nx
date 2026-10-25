// Host end-to-end tests for the MCP endpoint: real HTTP request parsing and
// mcp_handle_post over a socketpair, with input/screen stubbed (stubs.c).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "base64.h"
#include "buttons.h"
#include "config.h"
#include "http.h"
#include "mcp.h"

extern uint64_t stub_tap_mask;
extern int stub_tap_duration;
extern int stub_tap_count;

// Issues one POST /mcp request with the given JSON body; returns the raw HTTP
// response in a static buffer.
static const char *do_rpc(const char *body) {
    static char resp[256 * 1024];
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    // The handler writes the entire response before the test reads it back;
    // grow the buffers so large payloads (tools/list, screenshots) don't
    // deadlock the single-threaded harness.
    int bufsz = 512 * 1024;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));

    char req[64 * 1024];
    int rn = snprintf(req, sizeof(req),
                      "POST /mcp HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s",
                      strlen(body), body);
    assert(rn > 0 && write(sv[1], req, (size_t)rn) == rn);
    shutdown(sv[1], SHUT_WR);

    static HttpRequest hreq;
    assert(http_read_request(sv[0], &hreq));
    assert(strcmp(hreq.path, "/mcp") == 0);
    mcp_handle_post(&hreq);
    close(sv[0]);

    size_t total = 0;
    ssize_t n;
    while ((n = read(sv[1], resp + total, sizeof(resp) - 1 - total)) > 0)
        total += (size_t)n;
    resp[total] = '\0';
    close(sv[1]);
    return resp;
}

static void test_initialize(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                           "\"params\":{\"protocolVersion\":\"2025-03-26\","
                           "\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"0\"}}}");
    assert(strstr(r, "HTTP/1.1 200"));
    assert(strstr(r, "\"id\":1"));
    assert(strstr(r, "\"protocolVersion\":\"2025-03-26\"")); // echoed known version
    assert(strstr(r, "\"serverInfo\":{\"name\":\"sys-autopilot-nx\""));
    assert(strstr(r, "\"tools\":{}"));
    printf("initialize ok\n");
}

static void test_notification(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    assert(strstr(r, "HTTP/1.1 202"));
    printf("notification ok\n");
}

static void test_ping_and_errors(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"ping\"}");
    assert(strstr(r, "\"id\":\"abc\""));
    assert(strstr(r, "\"result\":{}"));

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"bogus/method\"}");
    assert(strstr(r, "\"error\":{\"code\":-32601"));

    r = do_rpc("this is not json");
    assert(strstr(r, "-32700"));

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"no_such_tool\"}}");
    assert(strstr(r, "\"error\":{\"code\":-32602"));
    printf("ping/errors ok\n");
}

static void test_tools_list(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}");
    assert(strstr(r, "\"tap_buttons\""));
    assert(strstr(r, "\"upload_file\""));
    assert(strstr(r, "\"hash_file\""));
    assert(strstr(r, "\"screenshot\""));
    assert(strstr(r, "\"inputSchema\""));
    printf("tools/list ok\n");
}

// sys-autopilot-nx removed these on purpose: they must be gone from
// tools/list and must not run when called by name.
static void test_removed_tools(void) {
    static const char *removed[] = {
        "create_token", "revoke_token", "sleep", "restart", "power_off",
        "get_theme", "set_theme", "get_nickname", "set_nickname",
        "get_brightness", "set_brightness", "get_volume", "set_volume",
        "airplane_mode", "list_installed_titles", "get_dns", "set_dns",
        "get_auto_time", "set_auto_time", "get_datetime", "set_datetime",
    };
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}");
    char quoted[64];
    for (size_t i = 0; i < sizeof(removed) / sizeof(removed[0]); i++) {
        snprintf(quoted, sizeof(quoted), "\"%s\"", removed[i]);
        assert(!strstr(r, quoted));
    }
    for (size_t i = 0; i < sizeof(removed) / sizeof(removed[0]); i++) {
        char body[256];
        snprintf(body, sizeof(body),
                 "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
                 "\"params\":{\"name\":\"%s\",\"arguments\":{}}}", removed[i]);
        r = do_rpc(body);
        assert(strstr(r, "\"error\":{\"code\":-32602,\"message\":\"unknown tool\""));
    }
    printf("removed tools ok\n");
}

static void test_tap_buttons(void) {
    stub_tap_count = 0;
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"tap_buttons\",\"arguments\":"
                           "{\"buttons\":[\"A\",\"home\"],\"durationMs\":5}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(stub_tap_count == 1);
    assert(stub_tap_mask == (BTN_A | BTN_HOME));
    assert(stub_tap_duration == 5);

    // Unknown button -> in-band tool error.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"tap_buttons\",\"arguments\":{\"buttons\":[\"Q\"]}}}");
    assert(strstr(r, "\"isError\":true"));
    printf("tap_buttons ok\n");
}

static void test_tap_sequence(void) {
    stub_tap_count = 0;
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"tap_sequence\",\"arguments\":{\"taps\":["
                           "{\"buttons\":[\"RIGHT\"],\"delayAfterMs\":1},"
                           "{\"buttons\":[\"RIGHT\"],\"delayAfterMs\":1},"
                           "{\"buttons\":[\"A\"],\"durationMs\":2}]}}}");
    assert(strstr(r, "performed 3 taps"));
    assert(stub_tap_count == 3);
    assert(stub_tap_mask == BTN_A);
    printf("tap_sequence ok\n");
}

static void test_screenshot(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"screenshot\",\"arguments\":{}}}");
    // base64("FAKEJPEGDATA")
    char expect[64];
    size_t n = b64_encode((const uint8_t *)"FAKEJPEGDATA", 12, expect);
    expect[n] = '\0';
    assert(strstr(r, "\"type\":\"image\""));
    assert(strstr(r, expect));
    assert(strstr(r, "\"mimeType\":\"image/jpeg\""));
    printf("screenshot ok\n");
}

static void test_upload_and_files(void) {
    system("rm -rf " FAKE_SD " && mkdir -p " FAKE_SD);

    // Upload with content BEFORE path (key-order robustness), then read back.
    const char *payload = "Hello, upload!\nLine two.";
    char b64[128];
    size_t n = b64_encode((const uint8_t *)payload, strlen(payload), b64);
    b64[n] = '\0';

    char body[512];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"upload_file\",\"arguments\":"
             "{\"content\":\"%s\",\"path\":\"/up/test.txt\"}}}", b64);
    const char *r = do_rpc(body);
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "wrote 24 bytes"));

    FILE *f = fopen(FAKE_SD "/up/test.txt", "rb");
    assert(f);
    char readback[64] = {0};
    size_t got = fread(readback, 1, sizeof(readback), f);
    fclose(f);
    assert(got == strlen(payload) && memcmp(readback, payload, got) == 0);

    // read_file round-trip (content contains a quote + newline after this write).
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"read_file\",\"arguments\":{\"path\":\"/up/test.txt\"}}}");
    assert(strstr(r, "Hello, upload!\\nLine two."));

    // read_file with negative offset (tail).
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"read_file\",\"arguments\":"
               "{\"path\":\"/up/test.txt\",\"offset\":-9}}}");
    assert(strstr(r, "Line two."));
    assert(!strstr(r, "Hello"));

    // list_directory.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"list_directory\",\"arguments\":{\"path\":\"/up\"}}}");
    assert(strstr(r, "test.txt"));
    assert(strstr(r, "\\\"type\\\":\\\"file\\\"")); // listing JSON is escaped text

    // delete_file + temp cleanup check.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"delete_file\",\"arguments\":{\"path\":\"/up/test.txt\"}}}");
    assert(strstr(r, "deleted"));
    struct stat st;
    assert(stat(FAKE_SD "/up/test.txt", &st) != 0);
    assert(stat(MCP_UPLOAD_TMP, &st) != 0); // no leftover temp file

    // Invalid base64 content -> tool error, no file created.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"upload_file\",\"arguments\":"
               "{\"content\":\"!!notbase64!!\",\"path\":\"/up/bad.bin\"}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(stat(FAKE_SD "/up/bad.bin", &st) != 0);
    assert(stat(MCP_UPLOAD_TMP, &st) != 0);

    // Path traversal rejected.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"read_file\",\"arguments\":{\"path\":\"/../etc/passwd\"}}}");
    assert(strstr(r, "\"isError\":true"));
    printf("upload/files ok\n");
}

static void test_hash_file(void) {
    system("rm -rf " FAKE_SD " && mkdir -p " FAKE_SD);

    // Write a known file directly (avoids depending on upload ordering here).
    const char *payload = "Hello, upload!\nLine two.";
    const char *expect_hex =
        "0266a67eaf1212ea81e14133d35c8df97d980c17c48ce4ada3c569308e2e2e4e";
    FILE *f = fopen(FAKE_SD "/h.txt", "wb");
    assert(f);
    fwrite(payload, 1, strlen(payload), f);
    fclose(f);

    // No 'expected': returns the digest + size, no 'matched' field.
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"hash_file\",\"arguments\":{\"path\":\"/h.txt\"}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "\\\"algorithm\\\":\\\"sha256\\\""));
    assert(strstr(r, expect_hex));
    assert(strstr(r, "\\\"size\\\":24"));
    assert(!strstr(r, "matched"));

    // Correct 'expected' -> matched:true.
    char body[256];
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":41,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"hash_file\",\"arguments\":"
             "{\"path\":\"/h.txt\",\"expected\":\"%s\"}}}", expect_hex);
    r = do_rpc(body);
    assert(strstr(r, "\\\"matched\\\":true"));

    // Uppercase 'expected' still matches (case-insensitive).
    snprintf(body, sizeof(body),
             "{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"tools/call\","
             "\"params\":{\"name\":\"hash_file\",\"arguments\":"
             "{\"path\":\"/h.txt\",\"expected\":\"0266A67EAF1212EA81E14133D35C8DF9"
             "7D980C17C48CE4ADA3C569308E2E2E4E\"}}}");
    r = do_rpc(body);
    assert(strstr(r, "\\\"matched\\\":true"));

    // Wrong 'expected' -> matched:false (still not an error).
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":43,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"hash_file\",\"arguments\":"
               "{\"path\":\"/h.txt\",\"expected\":\"deadbeef\"}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(strstr(r, "\\\"matched\\\":false"));

    // Missing file -> tool error.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":44,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"hash_file\",\"arguments\":{\"path\":\"/nope.txt\"}}}");
    assert(strstr(r, "\"isError\":true"));

    // Path traversal rejected.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":45,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"hash_file\",\"arguments\":{\"path\":\"/../etc/passwd\"}}}");
    assert(strstr(r, "\"isError\":true"));

    printf("hash_file ok\n");
}

static void test_input_with_screenshot(void) {
    // Input + screenshot in a single round trip: [text, image] content.
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\","
                           "\"params\":{\"name\":\"tap_buttons\",\"arguments\":"
                           "{\"buttons\":[\"A\"],\"durationMs\":1,"
                           "\"screenshot\":true,\"screenshotDelayMs\":1}}}");
    char expect[64];
    size_t n = b64_encode((const uint8_t *)"FAKEJPEGDATA", 12, expect);
    expect[n] = '\0';
    assert(strstr(r, "\"type\":\"text\",\"text\":\"ok\""));
    assert(strstr(r, "\"type\":\"image\""));
    assert(strstr(r, expect));
    assert(strstr(r, "\"isError\":false"));

    // Without the flag: text only, no image block.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"tap_buttons\",\"arguments\":"
               "{\"buttons\":[\"A\"],\"durationMs\":1}}}");
    assert(!strstr(r, "\"type\":\"image\""));
    printf("input+screenshot ok\n");
}

extern int stub_touch_count, stub_touch_x, stub_touch_y, stub_touch_duration;
extern int stub_swipe_count, stub_swipe_x1, stub_swipe_y1, stub_swipe_x2,
    stub_swipe_y2, stub_swipe_duration, stub_swipe_steps;

// sys-autopilot-nx touch tools. The argument rules themselves are covered in
// test_touch.c; this checks the MCP wiring.
static void test_touch_tools(void) {
    const char *r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":50,\"method\":\"tools/list\"}");
    assert(strstr(r, "\"name\":\"touch\""));
    assert(strstr(r, "\"name\":\"swipe\""));

    stub_touch_count = 0;
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":51,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"touch\",\"arguments\":{\"x\":100,\"y\":200}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(stub_touch_count == 1);
    assert(stub_touch_x == 100 && stub_touch_y == 200 && stub_touch_duration == 100);

    // Out of range -> in-band tool error, nothing touched.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":52,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"touch\",\"arguments\":{\"x\":1280,\"y\":200}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(strstr(r, "'x' must be between 0 and 1279"));
    assert(stub_touch_count == 1);

    // No arguments object at all.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":53,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"touch\"}}");
    assert(strstr(r, "\"isError\":true"));
    assert(strstr(r, "missing 'x'"));
    assert(stub_touch_count == 1);

    stub_swipe_count = 0;
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":54,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"swipe\",\"arguments\":{\"x1\":640,\"y1\":600,"
               "\"x2\":640,\"y2\":100,\"durationMs\":250,\"steps\":10}}}");
    assert(strstr(r, "\"isError\":false"));
    assert(stub_swipe_count == 1);
    assert(stub_swipe_x1 == 640 && stub_swipe_y1 == 600);
    assert(stub_swipe_x2 == 640 && stub_swipe_y2 == 100);
    assert(stub_swipe_duration == 250 && stub_swipe_steps == 10);

    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":55,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"swipe\",\"arguments\":{\"x1\":1,\"y1\":2,"
               "\"x2\":3,\"y2\":4}}}");
    assert(strstr(r, "\"isError\":true"));
    assert(strstr(r, "missing 'durationMs'"));
    assert(stub_swipe_count == 1);

    // Touch + screenshot in one round trip, like the button tools.
    r = do_rpc("{\"jsonrpc\":\"2.0\",\"id\":56,\"method\":\"tools/call\","
               "\"params\":{\"name\":\"touch\",\"arguments\":{\"x\":1,\"y\":1,"
               "\"screenshot\":true,\"screenshotDelayMs\":1}}}");
    assert(strstr(r, "\"type\":\"image\""));
    printf("touch tools ok\n");
}

int main(void) {
    test_initialize();
    test_notification();
    test_ping_and_errors();
    test_tools_list();
    test_removed_tools();
    test_touch_tools();
    test_tap_buttons();
    test_tap_sequence();
    test_screenshot();
    test_upload_and_files();
    test_hash_file();
    test_input_with_screenshot();
    printf("all mcp tests passed\n");
    return 0;
}
