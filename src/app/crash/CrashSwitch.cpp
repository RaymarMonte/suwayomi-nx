// The Switch half of crash containment (SAFETY.md S1, D13). libnx calls __libnx_exception_handler on the crashed thread, running
// on __nx_exception_stack, after the kernel has handed it the registers. Our diagAbortWithResult replaces libnx's (which would
// svcBreak into Atmosphère's crash reporter). These must live in the app's own object files, not in a static library: libnx
// refers to __libnx_exception_handler only weakly, and the linker never pulls a definition out of a library for a weak
// reference. tools/safety/check-elf.sh checks that ours are the ones linked.
// libnx is included here although this isn't platform/switch/: these symbols have to be in the executable itself (see above).

#include <switch.h>

#include <cstdint>
#include <cstring>

#include "app/crash/CrashDetail.hpp"
#include "core/CrashRecord.hpp"

namespace detail = snx::app::crash::detail;
namespace cr = snx::core::crashrecord;

extern "C" {

// The stack the exception handler runs on (S1: 32 KB; libnx's own default is 1 KB). One for the whole process: a second crash
// while one is handled ends the process at once (the re-entrancy guard in Crash.cpp), so two can't use it at the same time.
alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

// libnx's crt0 entry point: the very first byte of our .nro in memory (the NRO header follows it, see installPlatformHandlers).
// Not libnx's __start__: its linker script sets that to the fixed value 0, which the loader never moves, so it reads as 0
// at run time (found on the console in WP-03c: reading the header through it crashed every start).
extern char _start[];

void __libnx_exception_handler(ThreadExceptionDump* ctx);

}  // extern "C"

namespace {

Handle gMainThread = INVALID_HANDLE;
char gBuildId[41] = {};
std::uint64_t gBase = 0;
std::uint64_t gSize = 0;
char gWhat[256];

// Can [addr, addr + length) be read without crashing? Asks the kernel instead of trying.
bool readable(std::uint64_t addr, std::uint64_t length) noexcept {
    MemoryInfo info{};
    u32 pageInfo = 0;
    if (R_FAILED(svcQueryMemory(&info, &pageInfo, addr))) {
        return false;
    }
    if (info.type == MemType_Unmapped || (info.perm & Perm_R) == 0) {
        return false;
    }
    return addr + length <= info.addr + info.size;
}

// How far the .nro reaches in memory from `base`: its code, read-only data, data and .bss lie next to each other, all
// mapped as module memory by hbloader. Asks the kernel region by region and stops at the first one that isn't.
std::uint64_t moduleSizeFrom(std::uint64_t base) noexcept {
    std::uint64_t end = base;
    for (int region = 0; region < 8; region++) {
        MemoryInfo info{};
        u32 pageInfo = 0;
        if (R_FAILED(svcQueryMemory(&info, &pageInfo, end))) {
            break;
        }
        const u32 type = info.type & MemState_Type;
        if (type != MemType_ModuleCodeStatic && type != MemType_ModuleCodeMutable && type != MemType_CodeStatic &&
            type != MemType_CodeMutable) {
            break;
        }
        end = info.addr + info.size;
    }
    return end - base;
}

// One line in plain words from the exception's kind and the CPU's fault registers (ESR, FAR).
std::string_view describe(const ThreadExceptionDump* ctx) noexcept {
    cr::FixedText t(gWhat, sizeof(gWhat) - 1);
    const std::uint32_t errorClass = ctx->esr >> 26;
    const std::uint64_t far = ctx->far.x;
    switch (ctx->error_desc) {
        case ThreadExceptionDesc_InstructionAbort:
            t.text("instruction abort: jumped to a bad address ").hex(ctx->pc.x, 16);
            break;
        case ThreadExceptionDesc_MisalignedPC:
            t.text("misaligned program counter");
            break;
        case ThreadExceptionDesc_MisalignedSP:
            // libnx's name for the kernel's "unaligned data" (0x103): any misaligned data access. Only the CPU's error
            // class 0x26 means the stack pointer itself (seen on the console in WP-03c: the misaligned drill lands here).
            if (errorClass == 0x26) {
                t.text("misaligned stack pointer");
            } else {
                t.text("alignment fault at ").hex(far, 16);
            }
            break;
        case ThreadExceptionDesc_SError:
            t.text("SError (bus error)");
            break;
        case ThreadExceptionDesc_BadSVC:
            t.text("bad system call");
            break;
        case ThreadExceptionDesc_Trap:
            t.text("trap (illegal instruction)");
            break;
        default:
            if (errorClass == 0x24 || errorClass == 0x25) {  // data abort
                if ((ctx->esr & 0x3F) == 0x21) {
                    t.text("alignment fault at ").hex(far, 16);
                } else {
                    t.text("data abort at ").hex(far, 16);
                    if (far < 0x1000) {
                        t.text(" (null pointer?)");
                    }
                }
            } else if (errorClass == 0x00) {
                t.text("undefined instruction");
            } else {
                t.text("CPU exception, class ").hex(errorClass, 2);
            }
            break;
    }
    return t.view();
}

}  // namespace

extern "C" void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    detail::Registers regs;
    regs.valid = true;
    regs.pc = ctx->pc.x;
    regs.lr = ctx->lr.x;
    regs.sp = ctx->sp.x;
    regs.fp = ctx->fp.x;
    regs.far = ctx->far.x;
    regs.esr = ctx->esr;
    detail::fatal(detail::Kind::CpuException, describe(ctx), &regs, nullptr, false);
}

extern "C" void diagAbortWithResult(Result res) {
    if (!detail::installed()) {
        // Before main() (libnx's own start-up failed) or after the normal end: do what libnx does. svcBreak is a "user break",
        // which Atmosphère never turns into a system fatal, and its crash report keeps the result for debugging.
        svcBreak(BreakReason_Panic, reinterpret_cast<uintptr_t>(&res), sizeof(res));
        svcExitProcess();
    }
    detail::abortWithResult(res);
}

namespace snx::app::crash::detail {

void installPlatformHandlers() {
    // Nothing to switch on: the two hooks above are linked in and active from the first instruction (S12). This only
    // notes what the records need.
    gMainThread = threadGetCurHandle();
    gBase = reinterpret_cast<std::uint64_t>(_start);
    gSize = moduleSizeFrom(gBase);
    // elf2nro copies the .elf's GNU build id into the NRO header, which hbloader loads along with the code.
    const auto* header = reinterpret_cast<const NroHeader*>(_start + sizeof(NroStart));
    if (readable(reinterpret_cast<std::uint64_t>(header), sizeof(NroHeader)) && header->magic == NROHEADER_MAGIC) {
        static constexpr char kDigits[] = "0123456789abcdef";
        for (int i = 0; i < 20; i++) {
            gBuildId[2 * i] = kDigits[header->build_id[i] >> 4];
            gBuildId[2 * i + 1] = kDigits[header->build_id[i] & 0xF];
        }
    }
}

void moduleRange(std::uint64_t& base, std::uint64_t& size) noexcept {
    base = gBase;
    size = gSize;
}

std::string_view buildId() noexcept {
    return std::string_view(gBuildId, std::strlen(gBuildId));
}

std::size_t collectStack(const Registers* regs, std::uint64_t* out, std::size_t max) noexcept {
    // Walks the chain of frame records: x29 points at {caller's x29, return address}. Every step is checked with the kernel
    // first, so a broken chain just ends the list instead of crashing again.
    std::size_t n = 0;
    std::uint64_t fp = 0;
    if (regs != nullptr && regs->valid) {
        if (regs->lr != 0 && n < max) {
            out[n++] = regs->lr;
        }
        fp = regs->fp;
    } else {
        fp = reinterpret_cast<std::uint64_t>(__builtin_frame_address(0));
    }
    for (int step = 0; step < 64 && n < max; step++) {
        if (fp == 0 || (fp & 0xF) != 0 || !readable(fp, 16)) {
            break;
        }
        const std::uint64_t next = reinterpret_cast<const std::uint64_t*>(fp)[0];
        const std::uint64_t ret = reinterpret_cast<const std::uint64_t*>(fp)[1];
        if (ret == 0) {
            break;
        }
        if (!(n == 1 && step == 0 && out[0] == ret)) {  // the first record often repeats the lr register
            out[n++] = ret;
        }
        if (next <= fp) {
            break;  // callers' frames are higher up the stack: anything else is garbage
        }
        fp = next;
    }
    return n;
}

bool isMainThread() noexcept {
    return threadGetCurHandle() == gMainThread;
}

bool endProcessAfterCrash() noexcept {
    // exit() hands the CPU back to hbloader, which starts hbmenu in the same process with the same heap. Seen on the console
    // in WP-03c, both times with Atmosphère's crash reporter catching hbmenu: after a main-thread crash borealis's graphics
    // were still set up (a crash skips their shutdown), and after a worker crash the parked thread's stack was still mapped
    // out of the heap. Ending the process lets the system clean up everything instead.
    return true;
}

void parkThisThread() noexcept {
    for (;;) {
        svcSleepThread(INT64_MAX);
    }
}

void prepareThisThread() {
    // One process-wide exception stack (above): nothing to do per thread.
}

void drillMisaligned() {
    // An exclusive load must be aligned to its size on every ARMv8 CPU, whatever the system's alignment setting: a sure fault.
    alignas(16) static u8 buffer[32];
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(buffer) + 1;
    std::uint64_t value = 0;
    asm volatile("ldxr %0, [%1]" : "=r"(value) : "r"(address) : "memory");
    (void)value;
}

void drillGpuAbort() {
    diagAbortWithResult(MAKERESULT(cr::kGpuResultModule, 4));
}

}  // namespace snx::app::crash::detail
