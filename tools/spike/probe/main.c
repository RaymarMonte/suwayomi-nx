// WP-00 mode + memory probe (throwaway spike, see docs/wp/WP-00-toolchain-and-device-loop.md).
//
// Shows and saves the numbers the app's RuntimeProfile will be built from (D12, D21):
//   - the applet type: application mode (hold R on a game) or applet mode (Album)
//   - svcGetInfo total/used memory, and the heap size libnx got from hbloader
// Saves them as JSON to sdmc:/switch/suwayomi-nx/wp00/probe-<mode>.json (tmp file + rename),
// then waits for any button, or exits by itself after 20 s.
//
// Kept tiny on purpose: it also runs once from the Album (applet mode), where any crash is a
// system fatal. No GPU, no network, no allocation; plain text console only.
#include <stdio.h>
#include <sys/stat.h>
#include <switch.h>

// Set by libnx at start-up: the heap our malloc() uses.
extern char* fake_heap_start;
extern char* fake_heap_end;

static u64 processInfo(InfoType id, Result* rc)
{
    u64 value = 0;
    *rc = svcGetInfo(&value, id, CUR_PROCESS_HANDLE, 0);
    return value;
}

int main(int argc, char* argv[])
{
    consoleInit(NULL);
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeAny(&pad);

    AppletType type = appletGetAppletType();
    bool applicationMode = type == AppletType_Application || type == AppletType_SystemApplication;

    Result rcTotal, rcUsed, rcTotalNs, rcUsedNs;
    u64 total = processInfo(InfoType_TotalMemorySize, &rcTotal);
    u64 used = processInfo(InfoType_UsedMemorySize, &rcUsed);
    u64 totalNs = processInfo(InfoType_TotalNonSystemMemorySize, &rcTotalNs);
    u64 usedNs = processInfo(InfoType_UsedNonSystemMemorySize, &rcUsedNs);
    u64 heap = (u64)(fake_heap_end - fake_heap_start);
    u32 hos = hosversionGet();

    char json[1024];
    snprintf(json, sizeof(json),
             "{\"appletType\":%d,\"mode\":\"%s\",\"operationMode\":\"%s\",\"hos\":\"%u.%u.%u\",\n"
             " \"totalMemory\":%lu,\"usedMemory\":%lu,\"totalNonSystemMemory\":%lu,\"usedNonSystemMemory\":%lu,\n"
             " \"heap\":%lu,\"rc\":[\"0x%x\",\"0x%x\",\"0x%x\",\"0x%x\"],\"argv0\":\"%s\"}\n",
             (int)type, applicationMode ? "application" : "applet",
             appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld",
             HOSVER_MAJOR(hos), HOSVER_MINOR(hos), HOSVER_MICRO(hos),
             total, used, totalNs, usedNs, heap, rcTotal, rcUsed, rcTotalNs, rcUsedNs,
             argc > 0 ? argv[0] : "");

    // Only our own folder. Write a .tmp file, then rename it over the real name.
    const char* path = applicationMode ? "sdmc:/switch/suwayomi-nx/wp00/probe-application.json"
                                       : "sdmc:/switch/suwayomi-nx/wp00/probe-applet.json";
    char tmpPath[128];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    mkdir("sdmc:/switch/suwayomi-nx", 0777);
    mkdir("sdmc:/switch/suwayomi-nx/wp00", 0777);
    bool saved = false;
    FILE* f = fopen(tmpPath, "w");
    if (f) {
        saved = fputs(json, f) >= 0;
        saved = (fclose(f) == 0) && saved;
    }
    if (saved) {
        remove(path);
        saved = rename(tmpPath, path) == 0;
    }

    printf("suwayomi-nx WP-00 probe\n\n%s\n", json);
    printf("Total %lu MiB, used %lu MiB, heap %lu MiB\n\n", total >> 20, used >> 20, heap >> 20);
    printf("Saved: %s\n\n", saved ? path : "NO (write failed)");
    printf("Press any button to exit (or wait 20 s).\n");
    consoleUpdate(NULL);

    u64 start = armGetSystemTick();
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) != 0)
            break;
        if (armTicksToNs(armGetSystemTick() - start) > 20ULL * 1000000000ULL)
            break;
        consoleUpdate(NULL);
    }

    consoleExit(NULL);
    return 0;
}
