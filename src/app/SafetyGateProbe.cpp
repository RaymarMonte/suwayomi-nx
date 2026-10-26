// Only in the gate self-test build (tools/safety/gate-selftest.sh, -DSNX_SAFETY_GATE_PROBE=S2|S3|BOOST|BOOSTPTR), never in a
// real build. It links one banned function into the .elf on purpose, so tools/safety/check-elf.sh must fail the build and no
// .nro is made. Nothing ever calls snxSafetyGateProbe(): the linker keeps it only because of -Wl,--undefined
// (src/app/CMakeLists.txt).

#include <switch.h>

extern "C" void snxSafetyGateProbe() {
#if defined(SNX_SAFETY_GATE_PROBE_S2)
    fatalThrow(0);  // S2: the "press POWER to reboot" screen
#elif defined(SNX_SAFETY_GATE_PROBE_S3)
    setsysSetColorSetId(ColorSetId_Light);  // S3: writes a system setting
#elif defined(SNX_SAFETY_GATE_PROBE_BOOST)
    appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);  // S3 only-from: a call from somewhere other than libnx's exit code
#elif defined(SNX_SAFETY_GATE_PROBE_BOOSTPTR)
    // S3 only-from: no call at all, only the function's address stored in data. The gate must see that too.
    static Result (*volatile pointer)(ApmCpuBoostMode) = appletSetCpuBoostMode;
    (void)pointer;
#else
#error "SNX_SAFETY_GATE_PROBE must be S2, S3, BOOST or BOOSTPTR"
#endif
}
