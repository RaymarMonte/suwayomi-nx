#include "screen.h"
#include "log.h"

#include <string.h>

#define CAPTURE_TIMEOUT_NS 100000000ULL // 100ms

// libnx documents CAPSSC_JPEG_BUFFER_SIZE as the buffer size for this call.
// We keep it as-is: a too-small buffer is not worth a failed screenshot.
static u8 g_jpeg_buf[CAPSSC_JPEG_BUFFER_SIZE];
static bool g_caps_ok;

void screen_init(void) {
    Result rc = capsscInitialize();
    g_caps_ok = R_SUCCEEDED(rc);
    if (!g_caps_ok)
        LOGF("screen: capsscInitialize failed rc=0x%x; screenshots off\n", rc);
}

void screen_exit(void) {
    if (g_caps_ok) {
        capsscExit();
        g_caps_ok = false;
    }
}

bool screen_available(void) {
    return g_caps_ok;
}

Result screen_capture_jpeg(ViLayerStack stack, const u8 **out_buf, u64 *out_size) {
    if (!g_caps_ok)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    u64 size = 0;
    Result rc = capsscCaptureJpegScreenShot(&size, g_jpeg_buf, sizeof(g_jpeg_buf),
                                            stack, CAPTURE_TIMEOUT_NS);
    if (R_FAILED(rc)) {
        LOGF("screen: capture failed rc=0x%x\n", rc);
        return rc;
    }
    *out_buf = g_jpeg_buf;
    *out_size = size;
    return 0;
}

bool screen_parse_stack(const char *name, ViLayerStack *out) {
    if (strcasecmp(name, "screenshot") == 0)     *out = ViLayerStack_Screenshot;
    else if (strcasecmp(name, "default") == 0)   *out = ViLayerStack_Default;
    else if (strcasecmp(name, "lcd") == 0)       *out = ViLayerStack_Lcd;
    else if (strcasecmp(name, "recording") == 0) *out = ViLayerStack_Recording;
    else if (strcasecmp(name, "lastframe") == 0) *out = ViLayerStack_LastFrame;
    else return false;
    return true;
}
