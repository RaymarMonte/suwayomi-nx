#include "battery.h"

#ifdef __SWITCH__

#include <switch.h>

#include "log.h"

static bool g_psm_ok;

void battery_init(void) {
    g_psm_ok = R_SUCCEEDED(psmInitialize());
    if (!g_psm_ok)
        LOGF("battery: psm init failed\n");
}

void battery_exit(void) {
    if (g_psm_ok) {
        psmExit();
        g_psm_ok = false;
    }
}

bool battery_get(uint32_t *out_percent, bool *out_charging) {
    if (!g_psm_ok)
        return false;
    u32 pct = 0;
    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_FAILED(psmGetBatteryChargePercentage(&pct)))
        return false;
    psmGetChargerType(&charger); // best-effort
    *out_percent = pct;
    *out_charging = (charger != PsmChargerType_Unconnected);
    return true;
}

#else // host (tests): fixed values

void battery_init(void) {}
void battery_exit(void) {}

bool battery_get(uint32_t *out_percent, bool *out_charging) {
    *out_percent = 77;
    *out_charging = false;
    return true;
}

#endif
