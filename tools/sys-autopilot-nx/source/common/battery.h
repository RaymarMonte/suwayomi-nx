#pragma once

#include <stdbool.h>
#include <stdint.h>

// Read-only battery level for /status (psm service). This is all that is left
// of upstream's settings.c: sys-autopilot-nx never changes system settings.

// Opens psm. Must be called while an sm session is open (from __appInit).
// Best-effort: on failure /status simply omits the battery fields.
void battery_init(void);
void battery_exit(void);

// Battery charge in percent and whether a charger is connected. Returns false
// when psm is unavailable.
bool battery_get(uint32_t *out_percent, bool *out_charging);
