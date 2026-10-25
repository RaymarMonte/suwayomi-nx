#include "input.h"
#include "apiargs.h"
#include "buttons.h"
#include "log.h"

#include <assert.h>
#include <string.h>
#include <strings.h>

// apiargs.h repeats the duration limits so it stays host-testable.
static_assert(ARGS_DEFAULT_TAP_MS == INPUT_DEFAULT_TAP_MS, "tap default drift");
static_assert(ARGS_MAX_DURATION_MS == INPUT_MAX_DURATION_MS, "duration cap drift");

// buttons.h mirrors the libnx bit values so it stays host-testable; make
// sure they can never drift.
static_assert(BTN_A == HidNpadButton_A, "button mask drift");
static_assert(BTN_B == HidNpadButton_B, "button mask drift");
static_assert(BTN_X == HidNpadButton_X, "button mask drift");
static_assert(BTN_Y == HidNpadButton_Y, "button mask drift");
static_assert(BTN_LSTICK == HidNpadButton_StickL, "button mask drift");
static_assert(BTN_RSTICK == HidNpadButton_StickR, "button mask drift");
static_assert(BTN_L == HidNpadButton_L, "button mask drift");
static_assert(BTN_R == HidNpadButton_R, "button mask drift");
static_assert(BTN_ZL == HidNpadButton_ZL, "button mask drift");
static_assert(BTN_ZR == HidNpadButton_ZR, "button mask drift");
static_assert(BTN_PLUS == HidNpadButton_Plus, "button mask drift");
static_assert(BTN_MINUS == HidNpadButton_Minus, "button mask drift");
static_assert(BTN_LEFT == HidNpadButton_Left, "button mask drift");
static_assert(BTN_UP == HidNpadButton_Up, "button mask drift");
static_assert(BTN_RIGHT == HidNpadButton_Right, "button mask drift");
static_assert(BTN_DOWN == HidNpadButton_Down, "button mask drift");
static_assert(BTN_HOME == HiddbgNpadButton_Home, "button mask drift");
static_assert(BTN_CAPTURE == HiddbgNpadButton_Capture, "button mask drift");

static u8 g_workmem[0x1000] __attribute__((aligned(0x1000)));
static HiddbgHdlsSessionId g_session_id;
static HiddbgHdlsHandle g_handle;
static HiddbgHdlsState g_state;
static bool g_workbuf_attached;
static bool g_attached;

// --- touch screen (sys-autopilot-nx) -----------------------------------------
//
// hid:dbg's "touch screen autopilot" replaces what the touch panel reports:
// while a state is set, apps see exactly our fingers and real touches are
// ignored. We use one finger (id 0) and always end with touch_release().

// Fingertip size reported with each touch, in pixels (a real finger is in
// the same range; apps normally only look at x/y).
#define TOUCH_FINGER_DIAMETER 20
// After lifting the finger, wait this long before handing the panel back, so
// hid samples at least a few "no finger" frames and apps see the touch end.
#define TOUCH_LIFT_MS 50

// True from the first "finger down" until the panel was handed back.
static bool g_touch_active;

static Result touch_put_finger(int x, int y) {
    HidTouchState t;
    memset(&t, 0, sizeof(t));
    t.finger_id = 0;
    t.x = (u32)x;
    t.y = (u32)y;
    t.diameter_x = TOUCH_FINGER_DIAMETER;
    t.diameter_y = TOUCH_FINGER_DIAMETER;
    // Marked active before the call: if it fails halfway we still unset.
    g_touch_active = true;
    Result rc = hiddbgSetTouchScreenAutoPilotState(&t, 1);
    if (R_FAILED(rc))
        LOGF("input: touch at %d,%d failed rc=0x%x\n", x, y, rc);
    return rc;
}

// Lifts the finger (an empty touch list) and hands the panel back to real
// touches. With force, calls Unset even when we think no touch is active
// (used by /input/clear as a "make sure" reset); errors then only get logged.
static Result touch_release(bool force) {
    if (!g_touch_active && !force)
        return 0;
    bool was_active = g_touch_active;
    if (was_active) {
        HidTouchState none;
        memset(&none, 0, sizeof(none));
        Result lift = hiddbgSetTouchScreenAutoPilotState(&none, 0);
        if (R_FAILED(lift))
            LOGF("input: touch lift failed rc=0x%x\n", lift);
        svcSleepThread((s64)TOUCH_LIFT_MS * 1000000LL);
    }
    Result rc = hiddbgUnsetTouchScreenAutoPilotState();
    if (R_FAILED(rc)) {
        // Stay "active" so the next touch call or /input/clear retries.
        LOGF("input: touch unset failed rc=0x%x\n", rc);
        return was_active ? rc : 0;
    }
    g_touch_active = false;
    return 0;
}

// The HDLS work buffer is transfer memory mapped into the hid sysmodule.
// It is attached lazily (first input request) and MUST be released across
// sleep: holding it during a sleep transition breaks hid's own power
// handling and crashes the console (omm aborts with psc error 2165-1001).
static Result ensure_workbuf(void) {
    if (g_workbuf_attached)
        return 0;
    Result rc = hiddbgAttachHdlsWorkBuffer(&g_session_id, g_workmem, sizeof(g_workmem));
    if (R_FAILED(rc)) {
        LOGF("input: hiddbgAttachHdlsWorkBuffer failed rc=0x%x\n", rc);
        return rc;
    }
    g_workbuf_attached = true;
    return 0;
}

void input_suspend(void) {
    // Touch calls finish before the server looks at sleep events, so no
    // finger should be down here. Hand the panel back anyway, just in case.
    touch_release(false);
    if (g_attached) {
        hiddbgDetachHdlsVirtualDevice(g_handle);
        g_attached = false;
    }
    if (g_workbuf_attached) {
        hiddbgReleaseHdlsWorkBuffer(g_session_id);
        g_workbuf_attached = false;
    }
    memset(&g_state, 0, sizeof(g_state));
    g_state.battery_level = 4;
}

void input_exit(void) {
    input_suspend();
}

Result input_attach(void) {
    Result rc = ensure_workbuf();
    if (R_FAILED(rc))
        return rc;
    if (g_attached)
        return 0;

    HiddbgHdlsDeviceInfo device = {0};
    device.deviceType = HidDeviceType_FullKey3; // Pro Controller
    device.npadInterfaceType = HidNpadInterfaceType_Bluetooth;
    device.singleColorBody = RGBA8_MAXALPHA(60, 60, 60);
    device.singleColorButtons = RGBA8_MAXALPHA(230, 230, 230);
    device.colorLeftGrip = RGBA8_MAXALPHA(60, 60, 230);
    device.colorRightGrip = RGBA8_MAXALPHA(230, 60, 60);

    rc = hiddbgAttachHdlsVirtualDevice(&g_handle, &device);
    if (R_FAILED(rc)) {
        LOGF("input: hiddbgAttachHdlsVirtualDevice failed rc=0x%x\n", rc);
        return rc;
    }
    g_attached = true;

    // Push an initial neutral state so the controller registers.
    memset(&g_state, 0, sizeof(g_state));
    g_state.battery_level = 4;
    return hiddbgSetHdlsState(g_handle, &g_state);
}

Result input_detach(void) {
    if (!g_attached)
        return 0;
    Result rc = hiddbgDetachHdlsVirtualDevice(g_handle);
    if (R_SUCCEEDED(rc))
        g_attached = false;
    return rc;
}

bool input_is_attached(void) {
    return g_attached;
}

static Result apply_state(void) {
    return hiddbgSetHdlsState(g_handle, &g_state);
}

static int clamp_duration(int ms) {
    if (ms <= 0)
        return INPUT_DEFAULT_TAP_MS;
    if (ms > INPUT_MAX_DURATION_MS)
        return INPUT_MAX_DURATION_MS;
    return ms;
}

Result input_tap(u64 mask, int duration_ms) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;

    duration_ms = clamp_duration(duration_ms);

    g_state.buttons |= mask;
    rc = apply_state();
    if (R_FAILED(rc))
        return rc;

    svcSleepThread((s64)duration_ms * 1000000LL);

    g_state.buttons &= ~mask;
    return apply_state();
}

Result input_hold(u64 mask) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;
    g_state.buttons |= mask;
    return apply_state();
}

Result input_release(u64 mask) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;
    g_state.buttons &= ~mask;
    return apply_state();
}

Result input_clear(void) {
    // Always give the touch panel back (sys-autopilot-nx), even when no
    // touch is known to be active.
    Result rc_touch = touch_release(true);
    if (!g_attached)
        return rc_touch;
    g_state.buttons = 0;
    memset(&g_state.analog_stick_l, 0, sizeof(g_state.analog_stick_l));
    memset(&g_state.analog_stick_r, 0, sizeof(g_state.analog_stick_r));
    Result rc = apply_state();
    return R_FAILED(rc_touch) ? rc_touch : rc;
}

static s32 stick_value(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (s32)(v * JOYSTICK_MAX);
}

Result input_stick(int side, float x, float y, int duration_ms) {
    Result rc = input_attach();
    if (R_FAILED(rc))
        return rc;

    HidAnalogStickState *stick = (side == 0) ? &g_state.analog_stick_l
                                             : &g_state.analog_stick_r;
    stick->x = stick_value(x);
    stick->y = stick_value(y);
    rc = apply_state();
    if (R_FAILED(rc))
        return rc;

    if (duration_ms > 0) {
        if (duration_ms > INPUT_MAX_DURATION_MS)
            duration_ms = INPUT_MAX_DURATION_MS;
        svcSleepThread((s64)duration_ms * 1000000LL);
        stick->x = 0;
        stick->y = 0;
        rc = apply_state();
    }
    return rc;
}

Result input_touch(int x, int y, int duration_ms) {
    duration_ms = clamp_duration(duration_ms);
    Result rc = touch_put_finger(x, y);
    if (R_SUCCEEDED(rc))
        svcSleepThread((s64)duration_ms * 1000000LL);
    Result rc_release = touch_release(false);
    return R_FAILED(rc) ? rc : rc_release;
}

Result input_swipe(int x1, int y1, int x2, int y2, int duration_ms, int steps) {
    duration_ms = clamp_duration(duration_ms);
    if (steps < 1)
        steps = 1;
    if (steps > TOUCH_MAX_STEPS)
        steps = TOUCH_MAX_STEPS;
    s64 step_ns = (s64)duration_ms * 1000000LL / steps;

    // steps + 1 positions: the start point, then `steps` moves to the end.
    Result rc = 0;
    for (int i = 0; i <= steps; i++) {
        int x = x1 + (x2 - x1) * i / steps;
        int y = y1 + (y2 - y1) * i / steps;
        rc = touch_put_finger(x, y);
        if (R_FAILED(rc))
            break;
        if (i < steps)
            svcSleepThread(step_ns);
    }
    Result rc_release = touch_release(false);
    return R_FAILED(rc) ? rc : rc_release;
}
