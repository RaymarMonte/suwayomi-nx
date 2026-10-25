#pragma once

#include <string>
#include <string_view>

#include "core/RuntimeInfo.hpp"
#include "platform/SafeFs.hpp"

// Crash containment (docs/SAFETY.md S1, S8; D13). How it works, what each kind of crash ends in, and the record format:
// src/app/crash/README.md. In short: a crash anywhere in the app writes a crash record to logs/crash-<time>-<n>.txt and the
// app closes normally, so Atmosphère's crash reporter (and its "press POWER to reboot" screen) never gets involved.
namespace snx::app::crash {

// What the last run left behind (S1 "next launch").
struct LastRun {
    bool crashed = false;    // it left a crash record: this run is in safe mode
    std::string recordPath;  // where the newest record is now, e.g. "logs/crashes/crash-20261025-153007-1.txt"
    std::string type;        // from that record: "cpu_exception", ...
    std::string what;
};

// The very first thing main() does (S1, S12): installs the handlers (Switch: libnx's exception handler and
// diagAbortWithResult; desktop: signals) and std::set_terminate, opens the crash file, and moves the last run's records
// from logs/ to logs/crashes/ (the newest 10 are kept). Call once, on the main thread, before any other thread exists.
LastRun install(const platform::SafeFs& fs, core::LaunchMode mode);

// Every log line also goes into an in-memory ring, so a crash record can show the last 64. level: 'E', 'W', 'I', 'D', 'V'.
void pushLogLine(char level, std::string_view message) noexcept;

// Every thread we start calls this first (desktop: gives the thread its own stack for the crash handler; Switch: nothing).
void prepareThread();

// Shut-down steps for a crash on the main thread: they stop other threads (borealis's task thread now, our worker pools later),
// so that exit() leaves no thread running in an app that's gone. Each must end within a second or so. At most 8.
void addExitStep(void (*step)());
// The normal end of the main loop: borealis has stopped its threads itself, so the steps must not run any more.
void clearExitSteps();

// A C++ exception escaped one main-loop iteration (caught in main()) or a worker task (caught at its task boundary).
// Writes a crash record and asks for the crash screen. The caller carries on; the app then closes from the crash screen.
void recordMainLoopException(std::string_view what);
void recordWorkerException(std::string_view what);

// The crash screen (S1: non-GPU crashes while the main thread is still healthy). True once a record was written and the
// main loop should show it. recordName() is the record's path for the screen text.
bool crashScreenWanted();
std::string recordName();

// The main-loop watchdog (S8): if the main loop stops for 10 s while the app has the focus, it writes a crash record and
// ends the process. beat() once per main-loop iteration; setFocused(false) while the app is in the background.
void startWatchdog();
void beat() noexcept;
void setFocused(bool focused) noexcept;
// Backstop for an exit that hangs: if the process still runs `ms` from now, the watchdog ends it at once.
void armExitDeadline(int ms) noexcept;
void stopWatchdog();

// The normal end of a run: stops the watchdog and deletes the unused crash file. After this, a crash is no longer recorded.
void finish();

}  // namespace snx::app::crash
