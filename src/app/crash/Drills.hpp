#pragma once

#include "core/TestConfig.hpp"
#include "platform/SafeFs.hpp"

// Crash drills (SAFETY.md "Crash drills", README.md "Drills"): deliberate crashes that prove each kind ends in a crash record
// and a clean exit. Test builds only: the test config names one, TestHooks starts it once the app is first idle.
// In release builds these functions do nothing.
namespace snx::app::drills {

// Runs the drill now. Main-thread drills crash right here (or throw into the main loop); worker drills start their thread.
void run(core::testconfig::Drill drill, const platform::SafeFs& fs);

// The cache-write drill: true once its file is being written (TestHooks reports it as a "drill" event).
bool cacheWriteInProgress();

// At quit: stops the cache-write drill (its half-written file is dropped, S5) and waits for its thread.
void stop();

}  // namespace snx::app::drills
