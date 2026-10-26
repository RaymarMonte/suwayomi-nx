// The shared half of crash containment (SAFETY.md S1, S8). The platform halves (CrashSwitch.cpp, CrashDesktop.cpp) catch the
// crash and call detail::fatal(); this file writes the record and decides how the app ends. README.md has the whole picture.
// Rule for everything a crash can reach: no allocation, no locks that the crashed code might hold, no borealis.

#include "app/crash/Crash.hpp"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <borealis.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <mutex>
#include <optional>
#include <vector>

#include "app/crash/CrashDetail.hpp"
#include "core/CrashRecord.hpp"
#include "core/Version.hpp"
#include "platform/Platform.hpp"

namespace snx::app::crash {

namespace {

namespace cr = core::crashrecord;
using detail::Kind;
using Clock = std::chrono::steady_clock;

constexpr const char* kPendingPath = "logs/crash-pending.tmp";  // opened at start-up, renamed to the record's name after a crash
constexpr std::size_t kKeepRecords = 10;                         // in logs/crashes/
constexpr std::size_t kMaxRecordBytes = 64 * 1024;
constexpr int kMaxExitSteps = 8;
constexpr int kWatchdogTickMs = 500;
constexpr int kWatchdogStallTicks = 20;               // 20 × 0.5 s = 10 s (S8)
constexpr std::size_t kWatchdogStackBytes = 64 * 1024;  // explicit thread stack size (S8)
constexpr int kCrashExitDeadlineMs = 3000;

// Set up by install(). After a crash only read, plus the file and the buffers.
std::optional<platform::SafeFs> gFs;
platform::SafeFs::CrashFile gFile;
cr::LogRing gRing;
char gRecord[32 * 1024];
char gWhat[512];
std::string_view gMode = "application";
Clock::time_point gStart = Clock::now();
std::tm gLaunchUtc{};
int gRecordNumber = 0;
std::string gRecordName;  // "logs/crash-<time>-<n>.txt": the name the pending file gets when a record is written

std::atomic<bool> gActive{false};     // between install() and finish()
std::atomic<int> gEntered{0};         // crashes that started being handled. The second one ends the process at once
std::atomic<bool> gCrashScreen{false};

void (*gExitSteps[kMaxExitSteps])() = {};
std::atomic<int> gExitStepCount{0};

std::atomic<std::uint64_t> gBeats{0};
std::atomic<bool> gFocused{true};
std::atomic<std::int64_t> gExitDeadlineMs{0};  // uptime in ms when the backstop ends the process, 0 = not armed
std::mutex gWatchdogMutex;
std::condition_variable gWatchdogWake;
bool gWatchdogStop = false;
bool gWatchdogRunning = false;
pthread_t gWatchdogThread{};

std::int64_t uptimeMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - gStart).count();
}

std::string_view kindName(Kind kind) noexcept {
    switch (kind) {
        case Kind::CpuException:
            return "cpu_exception";
        case Kind::Abort:
            return "abort";
        case Kind::GpuAbort:
            return "gpu_abort";
        case Kind::Terminate:
            return "terminate";
        case Kind::MainLoopException:
            return "main_loop_exception";
        case Kind::WorkerException:
            return "worker_exception";
        case Kind::Watchdog:
            return "watchdog";
    }
    return "unknown";
}

// Opens the pending file and decides the name the next record gets.
bool prepareNextRecord() {
    gRecordNumber++;
    std::string name = "logs/" + cr::crashFileName(gLaunchUtc, gRecordNumber);
    if (!gFs->prepareCrashFile(gFile, kPendingPath, name)) {
        return false;
    }
    gRecordName = std::move(name);
    return true;
}

// Writes the crash record into the pending file and gives it its name. Allocation-free.
bool writeRecord(Kind kind, std::string_view what, const detail::Registers* regs, const std::uint32_t* result,
                 std::string_view thread) noexcept {
    if (!gFile.isOpen()) {
        return false;
    }
    static std::uint64_t stack[32];
    cr::CrashInfo info;
    info.type = kindName(kind);
    info.what = what;
    info.thread = thread;
    info.version = core::appVersion();
    info.flavor = core::buildFlavor();
    info.platform = platform::platformName();
    info.mode = gMode;
    info.buildId = detail::buildId();
    info.uptimeMs = static_cast<std::uint64_t>(uptimeMs());
    if (result != nullptr) {
        info.hasResult = true;
        info.result = *result;
    }
    if (regs != nullptr && regs->valid) {
        info.hasRegisters = true;
        info.pc = regs->pc;
        info.lr = regs->lr;
        info.sp = regs->sp;
        info.fp = regs->fp;
        info.far = regs->far;
        info.esr = regs->esr;
    }
    detail::moduleRange(info.moduleBase, info.moduleSize);
    info.stackCount = detail::collectStack(regs, stack, sizeof(stack) / sizeof(stack[0]));
    info.stack = stack;
    info.log = &gRing;
    const std::size_t n = cr::formatCrashRecord(info, gRecord, sizeof(gRecord));
    const bool written = gFile.write(gRecord, n);
    return gFile.finish() && written;
}

void runExitSteps() noexcept {
    const int count = gExitStepCount.load();
    for (int i = 0; i < count; i++) {
        try {
            gExitSteps[i]();
        } catch (...) {
            // a step that fails just doesn't stop its thread; the exit deadline still ends the process in time
        }
    }
}

void stopWatchdogQuietly() noexcept {
    try {
        stopWatchdog();
    } catch (...) {
    }
}

[[noreturn]] void onTerminate() noexcept {
    std::string_view what = "std::terminate() was called";
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            cr::FixedText t(gWhat, sizeof(gWhat) - 1);
            t.text("uncaught exception: ").clean(e.what());
            what = t.view();
        } catch (...) {
            what = "uncaught exception of an unknown type";
        }
    }
    detail::fatal(Kind::Terminate, what, nullptr, nullptr, false);
}

// The last run's records: logs/crash-*.txt → logs/crashes/. Keeps the newest kKeepRecords there.
LastRun fileAwayLastRun(const platform::SafeFs& fs) {
    LastRun last;
    fs.makeDirs("logs/crashes");
    // A pending file with something in it: a record was written but the rename didn't happen (power loss, a second crash).
    if (const auto pending = fs.readFile(kPendingPath, kMaxRecordBytes); pending && !pending->empty()) {
        fs.renameFile(kPendingPath, "logs/" + cr::crashFileName(gLaunchUtc, 0));
    } else if (fs.exists(kPendingPath)) {
        fs.removeFile(kPendingPath);
    }
    std::vector<std::string> records;
    for (const std::string& name : fs.listDir("logs")) {
        if (cr::isCrashFileName(name)) {
            records.push_back(name);
        }
    }
    std::sort(records.begin(), records.end());  // the names start with the time, so this is oldest first
    for (const std::string& name : records) {
        fs.renameFile("logs/" + name, "logs/crashes/" + name);
    }
    if (!records.empty()) {
        last.crashed = true;
        last.recordPath = "logs/crashes/" + records.back();
        if (const auto text = fs.readFile(last.recordPath, kMaxRecordBytes)) {
            if (const auto summary = cr::parseCrashRecord(*text)) {
                last.type = summary->type;
                last.what = summary->what;
            }
        }
    }
    std::vector<std::string> kept;
    for (const std::string& name : fs.listDir("logs/crashes")) {
        if (cr::isCrashFileName(name)) {
            kept.push_back(name);
        }
    }
    std::sort(kept.begin(), kept.end());
    for (std::size_t i = 0; i + kKeepRecords < kept.size(); i++) {
        fs.removeFile("logs/crashes/" + kept[i]);
    }
    return last;
}

void* watchdogMain(void*) {
    detail::prepareThisThread();
    std::uint64_t lastBeats = gBeats.load();
    int missed = 0;
    std::unique_lock<std::mutex> lock(gWatchdogMutex);
    while (!gWatchdogStop) {
        // Counting ticks instead of measuring time: if the whole console sleeps, this thread sleeps too and counts one tick.
        gWatchdogWake.wait_for(lock, std::chrono::milliseconds(kWatchdogTickMs));
        if (gWatchdogStop) {
            break;
        }
        const std::int64_t deadline = gExitDeadlineMs.load();
        if (deadline != 0 && uptimeMs() >= deadline) {
            platform::exitProcessNow(EXIT_FAILURE);  // an exit after a crash hangs: end it (README "the exit deadline")
        }
        const std::uint64_t beats = gBeats.load();
        if (beats != lastBeats || !gFocused.load()) {
            lastBeats = beats;
            missed = 0;
            continue;
        }
        if (++missed >= kWatchdogStallTicks) {
            lock.unlock();
            detail::fatal(Kind::Watchdog, "the main loop stopped for 10 s (watchdog)", nullptr, nullptr, false);
        }
    }
    return nullptr;
}

void recordCaughtException(Kind kind, std::string_view what, std::string_view thread) {
    if (!gActive.load()) {
        return;
    }
    if (gEntered.fetch_add(1) != 0) {
        // Already handling a crash (the crash screen itself keeps failing): nothing sensible is left to show.
        platform::exitProcessNow(EXIT_FAILURE);
    }
    cr::FixedText t(gWhat, sizeof(gWhat) - 1);
    t.clean(what);
    const bool written = writeRecord(kind, t.view(), nullptr, nullptr, thread);
    brls::Logger::error("crash: {} ({}). Crash record: {}", std::string(kindName(kind)), std::string(what),
                        written ? gRecordName : std::string("could not be written"));
    gCrashScreen.store(true);
}

}  // namespace

namespace detail {

bool installed() noexcept {
    return gActive.load();
}

void fatal(Kind kind, std::string_view what, const Registers* regs, const std::uint32_t* result, bool fromSignal) noexcept {
    if (gEntered.fetch_add(1) != 0 || !gActive.load()) {
        // A crash while handling one (re-entrancy guard, S1), or outside install()..finish(): end at once.
        platform::exitProcessNow(EXIT_FAILURE);
    }
    const bool mainThread = isMainThread();
    const std::string_view thread = kind == Kind::Watchdog ? "watchdog" : (mainThread ? "main" : "other");
    writeRecord(kind, what, regs, result, thread);

    if (kind == Kind::Watchdog) {
        platform::exitProcessNow(EXIT_FAILURE);  // the main thread is stuck, so nothing else can end the app
    }
    if (!mainThread) {
        if (kind != Kind::GpuAbort) {
            gCrashScreen.store(true);  // the main thread is fine: it shows the crash screen and quits normally
            parkThisThread();
        }
        platform::exitProcessNow(EXIT_FAILURE);
    }
    if (fromSignal) {
        platform::exitProcessNow(EXIT_FAILURE);  // desktop signal handler: only _exit() is safe there
    }
    if (endProcessAfterCrash()) {
        platform::exitProcessNow(EXIT_FAILURE);  // Switch: the system cleans up (D13 as amended, CrashSwitch.cpp says why)
    }
    // The main thread on the desktop, in ordinary code: stop the other threads, then exit() like D13 says. The deadline
    // ends the process if any of that hangs.
    armExitDeadline(kCrashExitDeadlineMs);
    runExitSteps();
    stopWatchdogQuietly();
    std::exit(EXIT_FAILURE);
}

void abortWithResult(std::uint32_t result) noexcept {
    const bool gpu = cr::resultModule(result) == cr::kGpuResultModule;
    cr::FixedText t(gWhat, sizeof(gWhat) - 1);
    t.text(gpu ? "GPU error (deko3d), result " : "abort with result ").hex(result, 8);
    fatal(gpu ? Kind::GpuAbort : Kind::Abort, t.view(), nullptr, &result, false);
}

}  // namespace detail

LastRun install(const platform::SafeFs& fs, core::LaunchMode mode) {
    gStart = Clock::now();
    detail::installPlatformHandlers();
    std::set_terminate(onTerminate);
    gMode = core::launchModeName(mode);
    gFs.emplace(fs);
    const std::time_t now = std::time(nullptr);
    gmtime_r(&now, &gLaunchUtc);
    LastRun last = fileAwayLastRun(*gFs);
    prepareNextRecord();  // if this fails, crashes still end cleanly, just without a record
    gActive.store(true);
    return last;
}

void pushLogLine(char level, std::string_view message) noexcept {
    char line[cr::LogRing::kLineBytes];
    cr::FixedText t(line, sizeof(line));
    t.dec(static_cast<std::uint64_t>(uptimeMs())).text(" ").text(std::string_view(&level, 1)).text(" ").clean(message);
    gRing.push(t.view());
}

void prepareThread() {
    detail::prepareThisThread();
}

void addExitStep(void (*step)()) {
    const int count = gExitStepCount.load();
    if (count < kMaxExitSteps) {
        gExitSteps[count] = step;
        gExitStepCount.store(count + 1);
    }
}

void clearExitSteps() {
    gExitStepCount.store(0);
}

void recordMainLoopException(std::string_view what) {
    recordCaughtException(Kind::MainLoopException, what, "main");
}

void recordWorkerException(std::string_view what) {
    recordCaughtException(Kind::WorkerException, what, "other");
}

bool crashScreenWanted() {
    return gCrashScreen.load();
}

std::string recordName() {
    return gRecordName;
}

void startWatchdog() {
    std::lock_guard<std::mutex> lock(gWatchdogMutex);
    if (gWatchdogRunning) {
        return;
    }
    gWatchdogStop = false;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kWatchdogStackBytes);
    gWatchdogRunning = pthread_create(&gWatchdogThread, &attr, watchdogMain, nullptr) == 0;
    pthread_attr_destroy(&attr);
    if (!gWatchdogRunning) {
        brls::Logger::warning("crash: the watchdog thread could not be started");
    }
}

void beat() noexcept {
    gBeats.fetch_add(1, std::memory_order_relaxed);
}

void setFocused(bool focused) noexcept {
    gFocused.store(focused);
}

void armExitDeadline(int ms) noexcept {
    gExitDeadlineMs.store(uptimeMs() + ms);
}

void stopWatchdog() {
    {
        std::lock_guard<std::mutex> lock(gWatchdogMutex);
        if (!gWatchdogRunning) {
            return;
        }
        gWatchdogStop = true;
    }
    gWatchdogWake.notify_all();
    pthread_join(gWatchdogThread, nullptr);
    std::lock_guard<std::mutex> lock(gWatchdogMutex);
    gWatchdogRunning = false;
}

void finish() {
    stopWatchdog();
    gActive.store(false);
    gFile.discard();
}

void endAfterCrashScreen() {
    if (detail::endProcessAfterCrash()) {
        platform::exitProcessNow(EXIT_FAILURE);
    }
}

}  // namespace snx::app::crash
