#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Between the shared crash logic (Crash.cpp) and the platform halves (CrashSwitch.cpp, CrashDesktop.cpp). Not for other code.
// Everything that runs after a crash is noexcept and allocation-free.
namespace snx::app::crash::detail {

enum class Kind { CpuException, Abort, GpuAbort, Terminate, MainLoopException, WorkerException, Watchdog };

struct Registers {
    bool valid = false;
    std::uint64_t pc = 0, lr = 0, sp = 0, fp = 0, far = 0;
    std::uint32_t esr = 0;
};

// --- shared (Crash.cpp) ---

// True between install() and finish(): only then is there a crash file to write to.
bool installed() noexcept;
// Writes the crash record and ends the way src/app/crash/README.md says for this kind of crash. Never returns.
// fromSignal: called inside a desktop signal handler, where only the simplest calls are safe.
[[noreturn]] void fatal(Kind kind, std::string_view what, const Registers* regs, const std::uint32_t* result,
                        bool fromSignal) noexcept;
// diagAbortWithResult's work (the Switch override calls it; the desktop gpu-abort drill too). GPU results skip the crash screen.
[[noreturn]] void abortWithResult(std::uint32_t result) noexcept;

// --- per platform (CrashSwitch.cpp / CrashDesktop.cpp) ---

// Exception handler / signal handlers. Called by install() first thing.
void installPlatformHandlers();
// Where the app's own code and data are in memory (0, 0 if unknown), and the GNU build id as hex (empty if unknown).
void moduleRange(std::uint64_t& base, std::uint64_t& size) noexcept;
std::string_view buildId() noexcept;
// Return addresses of the calling thread (regs == nullptr) or of the crashed code (regs from the exception). Innermost first.
std::size_t collectStack(const Registers* regs, std::uint64_t* out, std::size_t max) noexcept;
bool isMainThread() noexcept;
// Sleeps forever: a crashed worker thread waits here while the main thread shows the crash screen and quits.
[[noreturn]] void parkThisThread() noexcept;
// Per-thread set-up (desktop: alternate signal stack).
void prepareThisThread();
// The two drills that need the platform: a misaligned access and a GPU-style abort (test builds).
void drillMisaligned();
[[noreturn]] void drillGpuAbort();

}  // namespace snx::app::crash::detail
