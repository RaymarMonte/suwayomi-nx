#pragma once

#include "core/RuntimeProfile.hpp"
#include "platform/SafeFs.hpp"

// The app's log (WP-08). All code logs with brls::Logger ("info", "warning", ...), starting the message with a short tag for
// its module or screen, e.g. brls::Logger::info("setup: testing {}", url). Every line then goes three ways:
//   - stdout (borealis does that itself: the desktop E2E harness keeps it as stdout.txt, nxlink shows it on the PC),
//   - the crash handler's ring of the last 64 lines (SAFETY.md S1), right away,
//   - logs/app.log on the SD card, buffered: written at most every RuntimeProfile::logFlushMs, and at exit, HOME and before
//     the applet-mode guard's screen. Each write is open -> append -> close (SafeFs::append), so no file stays open between
//     frames (S5) and the harness can read the log while the app runs. Before app.log would grow past logFileBytes it becomes
//     app.1.log; at start the last run's app.log becomes app.1.log, so a crashed run's log is still there afterwards.
// Line format (core::formatLogLine): "<ms since start> <E|W|I|D|V> <message>".
namespace snx::app::logger {

// Once, right after crash::install() (main thread, before other threads). `fs` must outlive the logger.
void start(const platform::SafeFs& fs, const core::RuntimeProfile& profile, bool debug);

// Once per main-loop iteration: writes the waiting lines when they are due.
void onFrame();

// Writes the waiting lines now (exit, HOME/sleep, the applet-mode guard). Main thread.
void flush();

// The last flush at exit: after it, lines only go to stdout and the crash ring.
void stop();

}  // namespace snx::app::logger
