#include "app/crash/Drills.hpp"

#ifdef SNX_TEST_HOOKS

#include <pthread.h>

#include <atomic>
#include <borealis.hpp>
#include <chrono>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/crash/Crash.hpp"
#include "app/crash/CrashDetail.hpp"

namespace snx::app::drills {

namespace {

using core::testconfig::Drill;

constexpr std::size_t kWorkerStackBytes = 256 * 1024;  // the worker size from SAFETY.md S8
constexpr std::size_t kPieceBytes = 64 * 1024;
constexpr std::size_t kMaxCacheDrillBytes = 64 * 1024 * 1024;
constexpr std::size_t kOomBlockBytes = 2ULL * 1024 * 1024 * 1024;

std::atomic<bool> gCacheWriting{false};
std::atomic<bool> gStopCacheDrill{false};
bool gCacheThreadRunning = false;
pthread_t gCacheThread{};
const platform::SafeFs* gFs = nullptr;

void nullWrite() {
    volatile int* volatile pointer = nullptr;  // volatile twice: the compiler can't see it's null and drop the write
    *pointer = 1;
}

int recurse(int depth) {
    volatile char padding[1024];
    padding[0] = static_cast<char>(depth);
    if (depth < (1 << 30)) {  // never reached: the stack runs out long before
        return recurse(depth + 1) + padding[0];
    }
    return padding[0];
}

// The task boundary every worker thread has (S1): an exception becomes a crash record and the crash screen.
template <typename Body>
void guarded(Body body) {
    try {
        body();
    } catch (const std::exception& e) {
        crash::recordWorkerException(e.what());
    } catch (...) {
        crash::recordWorkerException("unknown exception");
    }
}

void* workerNullWrite(void*) {
    crash::prepareThread();
    guarded([] { nullWrite(); });
    return nullptr;
}

void* workerStackOverflow(void*) {
    crash::prepareThread();
    guarded([] { recurse(0); });
    return nullptr;
}

// Writes cache/drill.bin in pieces and never finishes on its own: it is still "in flight" when the app is asked to quit.
void* cacheWriter(void*) {
    crash::prepareThread();
    guarded([] {
        gFs->makeDirs("cache");
        auto writer = gFs->startPieces("cache/drill.bin");
        if (!writer.isOpen()) {
            throw std::runtime_error("cache drill: can't open cache/drill.bin.tmp");
        }
        const std::string piece(kPieceBytes, 'c');
        std::size_t written = 0;
        while (!gStopCacheDrill.load()) {
            if (written < kMaxCacheDrillBytes) {
                writer.write(piece);
                written += piece.size();
                gCacheWriting.store(true);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        writer.abandon();  // an exit request in the middle of a cache write: drop the file (S5)
    });
    return nullptr;
}

bool startThread(void* (*body)(void*), pthread_t* keep) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kWorkerStackBytes);
    pthread_t thread{};
    const bool ok = pthread_create(&thread, &attr, body, nullptr) == 0;
    pthread_attr_destroy(&attr);
    if (ok && keep != nullptr) {
        *keep = thread;
    } else if (ok) {
        pthread_detach(thread);  // it crashes and never comes back
    }
    return ok;
}

}  // namespace

void run(Drill drill, const platform::SafeFs& fs) {
    gFs = &fs;
    brls::Logger::warning("drill: {} (a deliberate crash, test builds only)", std::string(core::testconfig::drillName(drill)));
    switch (drill) {
        case Drill::None:
            return;
        case Drill::NullWrite:
            nullWrite();
            break;
        case Drill::Misaligned:
            crash::detail::drillMisaligned();
            break;
        case Drill::WorkerNullWrite:
            startThread(workerNullWrite, nullptr);
            return;
        case Drill::WorkerStackOverflow:
            startThread(workerStackOverflow, nullptr);
            return;
        case Drill::Terminate:
            std::terminate();
        case Drill::GpuAbort:
            crash::detail::drillGpuAbort();
        case Drill::Oom: {
            // 2 GB at a time until the allocator says no. The memory is never touched. std::bad_alloc goes to the main loop.
            std::vector<std::unique_ptr<char[]>> blocks;
            for (int i = 0; i < 64; i++) {
                blocks.emplace_back(new char[kOomBlockBytes]);
            }
            throw std::bad_alloc();  // 128 GB worked: still count it as running out
        }
        case Drill::MainLoopException:
            throw std::runtime_error("drill: an exception escaped the main loop");
        case Drill::Watchdog:
            std::this_thread::sleep_for(std::chrono::seconds(15));  // the watchdog ends the app after 10 s
            break;
        case Drill::CacheWriteExit:
            gStopCacheDrill.store(false);
            gCacheThreadRunning = startThread(cacheWriter, &gCacheThread);
            return;
    }
    brls::Logger::error("drill: {} did not crash the app", std::string(core::testconfig::drillName(drill)));
}

bool cacheWriteInProgress() {
    return gCacheWriting.load();
}

void stop() {
    if (gCacheThreadRunning) {
        gStopCacheDrill.store(true);
        pthread_join(gCacheThread, nullptr);
        gCacheThreadRunning = false;
    }
}

}  // namespace snx::app::drills

#else  // release build: no drills

namespace snx::app::drills {

void run(core::testconfig::Drill, const platform::SafeFs&) {}
bool cacheWriteInProgress() {
    return false;
}
void stop() {}

}  // namespace snx::app::drills

#endif
