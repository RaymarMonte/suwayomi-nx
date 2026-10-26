// The desktop half of crash containment. The PC has no libnx exception handler; POSIX signals (SIGSEGV, SIGBUS, SIGILL, SIGFPE,
// SIGABRT) play its part, so the shared logic, the record and the crash drills can be tested on the PC (README.md).
// Each thread gets its own alternate signal stack, so even a stack overflow can be recorded.

#include <execinfo.h>
#include <link.h>
#include <pthread.h>
#include <ucontext.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "app/crash/CrashDetail.hpp"
#include "core/CrashRecord.hpp"

namespace snx::app::crash::detail {

namespace {

namespace cr = core::crashrecord;

constexpr std::size_t kSignalStackBytes = 64 * 1024;

pthread_t gMainThread{};
char gBuildId[41] = {};
std::uint64_t gBase = 0;
std::uint64_t gSize = 0;
char gWhat[256];

// The first object dl_iterate_phdr reports is the program itself: its load address, size and GNU build id.
int readProgramHeaders(dl_phdr_info* info, std::size_t, void*) {
    gBase = info->dlpi_addr;
    std::uint64_t end = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD && ph.p_vaddr + ph.p_memsz > end) {
            end = ph.p_vaddr + ph.p_memsz;
        }
        if (ph.p_type != PT_NOTE) {
            continue;
        }
        const auto* note = reinterpret_cast<const unsigned char*>(info->dlpi_addr + ph.p_vaddr);
        const unsigned char* notesEnd = note + ph.p_memsz;
        while (note + sizeof(ElfW(Nhdr)) <= notesEnd) {
            const auto* header = reinterpret_cast<const ElfW(Nhdr)*>(note);
            const unsigned char* name = note + sizeof(ElfW(Nhdr));
            const unsigned char* desc = name + ((header->n_namesz + 3) & ~3u);
            if (header->n_type == NT_GNU_BUILD_ID && header->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0) {
                static constexpr char kDigits[] = "0123456789abcdef";
                for (std::uint32_t b = 0; b < header->n_descsz && b < 20; b++) {
                    gBuildId[2 * b] = kDigits[desc[b] >> 4];
                    gBuildId[2 * b + 1] = kDigits[desc[b] & 0xF];
                }
            }
            note = desc + ((header->n_descsz + 3) & ~3u);
        }
    }
    gSize = end;
    return 1;  // only the program, not the shared libraries
}

void onCrashSignal(int signal, siginfo_t* info, void* context) {
    cr::FixedText t(gWhat, sizeof(gWhat) - 1);
    const auto address = reinterpret_cast<std::uint64_t>(info->si_addr);
    switch (signal) {
        case SIGSEGV:
            t.text("SIGSEGV: bad memory access at ").hex(address, 16);
            if (address < 0x1000) {
                t.text(" (null pointer?)");
            }
            break;
        case SIGBUS:
            t.text("SIGBUS: bus error (misaligned or unmapped access) at ").hex(address, 16);
            break;
        case SIGILL:
            t.text("SIGILL: illegal instruction");
            break;
        case SIGFPE:
            t.text("SIGFPE: arithmetic error");
            break;
        default:
            t.text("SIGABRT: abort() was called");
            break;
    }
    Registers regs;
#if defined(__x86_64__)
    const auto* uc = static_cast<const ucontext_t*>(context);
    regs.valid = true;
    regs.pc = static_cast<std::uint64_t>(uc->uc_mcontext.gregs[REG_RIP]);
    regs.sp = static_cast<std::uint64_t>(uc->uc_mcontext.gregs[REG_RSP]);
    regs.fp = static_cast<std::uint64_t>(uc->uc_mcontext.gregs[REG_RBP]);
    regs.far = address;
#else
    (void)context;
#endif
    fatal(signal == SIGABRT ? Kind::Abort : Kind::CpuException, t.view(), &regs, nullptr, true);
}

}  // namespace

void installPlatformHandlers() {
    gMainThread = pthread_self();
    dl_iterate_phdr(readProgramHeaders, nullptr);
    void* warmUp[4];
    backtrace(warmUp, 4);  // loads libgcc's unwinder now, so the handler doesn't have to
    prepareThisThread();
    struct sigaction action {};
    action.sa_sigaction = onCrashSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
        sigaction(signal, &action, nullptr);
    }
}

void moduleRange(std::uint64_t& base, std::uint64_t& size) noexcept {
    base = gBase;
    size = gSize;
}

std::string_view buildId() noexcept {
    return std::string_view(gBuildId, std::strlen(gBuildId));
}

std::size_t collectStack(const Registers*, std::uint64_t* out, std::size_t max) noexcept {
    void* frames[64];
    const int count = backtrace(frames, static_cast<int>(max < 64 ? max : 64));
    for (int i = 0; i < count; i++) {
        out[i] = reinterpret_cast<std::uint64_t>(frames[i]);
    }
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}

bool isMainThread() noexcept {
    return pthread_equal(pthread_self(), gMainThread) != 0;
}

bool endProcessAfterCrash() noexcept {
    return false;  // D13's exit(): nothing runs after the process on the PC
}

void parkThisThread() noexcept {
    for (;;) {
        pause();
    }
}

void prepareThisThread() {
    stack_t current{};
    if (sigaltstack(nullptr, &current) == 0 && (current.ss_flags & SS_DISABLE) == 0) {
        return;  // this thread has one already
    }
    stack_t stack{};
    stack.ss_sp = std::malloc(kSignalStackBytes);  // kept for the thread's whole life
    stack.ss_size = kSignalStackBytes;
    if (stack.ss_sp != nullptr) {
        sigaltstack(&stack, nullptr);
    }
}

void drillMisaligned() {
    std::raise(SIGBUS);  // x86 has no alignment faults, so the drill raises the signal a misaligned access gives on ARM
}

void drillGpuAbort() {
    abortWithResult((4u << 9) | cr::kGpuResultModule);  // what diagAbortWithResult(MAKERESULT(359, 4)) does on the Switch
}

}  // namespace snx::app::crash::detail
