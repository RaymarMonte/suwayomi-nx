// D28: do-nothing stand-ins for two banned functions (SAFETY.md S3) that libraries reference but never call
// (docs/safety-baseline.txt). The app is linked with -Wl,--wrap=<name> for every name in tools/safety/stand-ins.txt, so each
// call to <name> inside libnx or borealis goes to __wrap_<name> here instead, and the real function never gets into the .elf.
// tools/safety/check-elf.sh then stays strict: the banned names must not be in the binary at all.
// A new stand-in needs the owner's OK (D28). Switch build only.

#include <switch.h>

extern "C" {

// libnx's appletInitialize() calls it only for a non-zero entry of __nx_applet_PerformanceConfiguration, and that array is
// libnx's own (all zero). Success, so start-up carries on exactly as before. D17: the app never changes performance modes.
Result __wrap_apmSetPerformanceConfiguration(ApmPerformanceMode, u32) {
    return 0;
}

// borealis's SwitchPlatform::setBacklightBrightness() calls it, and nothing calls that. The backlight stays the user's.
Result __wrap_lblSetCurrentBrightnessSetting(float) {
    return 0;
}

}  // extern "C"
