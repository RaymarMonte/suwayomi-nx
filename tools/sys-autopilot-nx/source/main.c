#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "common/battery.h"
#include "common/config.h"
#include "common/device_info.h"
#include "common/input.h"
#include "common/log.h"
#include "common/netif.h"
#include "common/power.h"
#include "common/screen.h"
#include "common/server.h"

// Inner heap: socket transfer memory + stdio buffers + dir listing JSON +
// MCP text responses. The large fixed buffers (JPEG, I/O, HDLS workmem) are
// static bss.
//
// sys-autopilot-nx: 640 KiB instead of upstream's 4 MiB (upstream PR #30
// tried 512 KiB). What lives here:
//   - always in use: socket transfer memory from socketInitialize():
//     (tcp_tx 0x8000 + tcp_rx 0x10000 + udp_tx 0x2400 + udp_rx 0xA500,
//     rounded up to 0x25000) x sb_efficiency 2 = 0x4A000 = 296 KiB.
//     Measured on the console: 304,768 bytes in use when idle.
//   - short-lived buffers: MCP read_file (up to 32 KiB + its JSON-escaped
//     copy), directory listings, MCP text results, stdio FILE buffers.
// Measured peak on the console (WP-00b): 458,672 bytes after reading a 32 KiB
// text log and a binary file over MCP. The one case that does not fit is MCP
// read_file of 32 KiB of pure control bytes (each byte escapes to 6), which
// answers "out of memory". Every allocation failure is handled that way: the
// request gets an error, nothing aborts. REST GET /files streams and uses no
// heap. /status reports the heap ("heap": size/peak/used).
#define INNER_HEAP_SIZE 0xA0000

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// sys-autopilot-nx rule for start-up: only sm, fs, hid:dbg and bsd may abort
// (that is a system fatal for a sysmodule). Everything else is optional: it
// logs, and its feature is switched off.

// False when PSC (sleep/wake) registration failed. Then we never open sockets
// and main() exits right away: live sockets without sleep handling crash the
// console on the next sleep.
static bool g_can_serve;

void __libnx_initheap(void)
{
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

// Keep socket buffers small to fit the sysmodule memory budget.
static const SocketInitConfig kSocketConfig = {
    .tcp_tx_buf_size     = 0x8000,
    .tcp_rx_buf_size     = 0x10000,
    .tcp_tx_buf_max_size = 0,       // fixed size
    .tcp_rx_buf_max_size = 0,       // fixed size
    .udp_tx_buf_size     = 0x2400,
    .udp_rx_buf_size     = 0xA500,
    .sb_efficiency       = 2,
    // Sessions in concurrent use: the TCP listener, one accepted client, and
    // the persistent mDNS/DNS-SD UDP socket. 3 is the minimum; use 4 for a
    // little headroom.
    .num_bsd_sessions    = 4,
    .bsd_service_type    = BsdServiceType_User,
};

void __appInit(void)
{
    Result rc;

    rc = smInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    rc = fsdevMountSdmc();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    rc = hiddbgInitialize();
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    // Optional: without caps:sc, /screenshot answers 503.
    screen_init();

    // Register with PSC so we can quiesce sockets across sleep (must happen
    // while the sm session is open).
    g_can_serve = power_init();

    if (g_can_serve) {
        rc = socketInitialize(&kSocketConfig);
        if (R_FAILED(rc))
            diagAbortWithResult(rc);
    }

    // Gather device facts (model/firmware) for the mDNS TXT record now, while
    // the sm session is still open: the underlying set:sys smGetService calls
    // would fail after smExit(). Best-effort; failures just leave the
    // corresponding TXT fields empty.
    device_info_init();

    // Network Interface Manager: nifm gives us the real LAN IP for mDNS
    // (gethostid() only ever returns loopback in a sysmodule). Must be opened
    // while sm is up; the session is held for the process lifetime.
    if (!netif_init())
        LOGF("netif: nifm init failed; mDNS A records unavailable\n");

    // Battery level for /status (psm). Best-effort.
    battery_init();

    smExit();
}

void __appExit(void)
{
    battery_exit();
    netif_exit();
    power_exit();
    if (g_can_serve)
        socketExit();
    screen_exit();
    hiddbgExit();
    fsdevUnmountAll();
    fsExit();
}

#ifdef __cplusplus
}
#endif

int main(int argc, char* argv[])
{
    // Never give freed heap back ("trim"): the heap is a fixed block inside
    // the program anyway, so trimming gains nothing. This keeps
    // mallinfo().arena growing only, so /status can report it as the peak.
    mallopt(M_TRIM_THRESHOLD, 0x7fffffff);

    Config cfg;
    config_load(&cfg);

    if (!g_can_serve) {
        LOGF("main: PSC registration failed; not serving (exiting cleanly)\n");
        return 0;
    }

    // Blocks forever (NULL idle callback).
    server_run(&cfg, NULL);

    input_exit();
    return 0;
}
