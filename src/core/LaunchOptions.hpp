#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snx::core {

// Command-line flags (desktop command line, or nxlink --args "..." on the Switch).
struct LaunchOptions {
    bool debugLog = false;            // -d: debug log level
    bool logToFile = false;           // --log-file: also write the log to <data>/logs/app.log, so device runs can read it back
    bool simulateAppletMode = false;  // --simulate-applet-mode: desktop test flag, act as if started from the Album (D21)
    std::uint64_t memLimitMb = 0;     // --mem-limit=<MB>: desktop test flag, pretend the app has only this much memory. 0 = no limit
    std::vector<std::string> problems;  // unknown flags and bad values, in plain words. They are ignored, and main() logs them
};

// Reads the flags above. argv[0] (the program path) is skipped. Never throws: anything it doesn't understand goes into `problems`.
LaunchOptions parseLaunchOptions(int argc, const char* const argv[]);

}  // namespace snx::core
