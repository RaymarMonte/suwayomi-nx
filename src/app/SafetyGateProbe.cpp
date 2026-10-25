// Only in the gate self-test build (tools/safety/gate-selftest.sh, -DSNX_SAFETY_GATE_PROBE=S2|S3), never in a real build.
// It links one banned function into the .elf on purpose, so tools/safety/check-elf.sh must fail the build and no .nro is made.
// Nothing ever calls snxSafetyGateProbe(): the linker keeps it only because of -Wl,--undefined (src/app/CMakeLists.txt).

#include <switch.h>

extern "C" void snxSafetyGateProbe() {
#if defined(SNX_SAFETY_GATE_PROBE_S2)
    fatalThrow(0);  // S2: the "press POWER to reboot" screen
#elif defined(SNX_SAFETY_GATE_PROBE_S3)
    setsysSetColorSetId(ColorSetId_Light);  // S3: writes a system setting
#else
#error "SNX_SAFETY_GATE_PROBE must be S2 or S3"
#endif
}
