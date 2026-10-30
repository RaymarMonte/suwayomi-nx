#include "core/LaunchOptions.hpp"

#include <string_view>

namespace snx::core {

namespace {

constexpr std::string_view kMemLimitPrefix = "--mem-limit=";
// 1 TiB in MB. A bigger number is surely a typo, and it keeps "MB * 1024 * 1024" far away from overflowing.
constexpr std::uint64_t kMaxMemLimitMb = 1024ULL * 1024ULL;

// Parses a whole decimal number with no sign or other characters. Returns false for anything else.
bool parseWholeNumber(std::string_view text, std::uint64_t& out) {
    if (text.empty() || text.size() > 12) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    out = value;
    return true;
}

}  // namespace

LaunchOptions parseLaunchOptions(int argc, const char* const argv[]) {
    LaunchOptions options;
    for (int i = 1; i < argc; i++) {
        if (argv[i] == nullptr) {
            continue;
        }
        const std::string_view arg(argv[i]);
        if (arg == "-d") {
            options.debugLog = true;
        } else if (arg == "--log-file") {
            options.logToFile = true;
        } else if (arg == "--simulate-applet-mode") {
            options.simulateAppletMode = true;
        } else if (arg == "--simulate-docked") {
            options.simulateDocked = true;
        } else if (arg.substr(0, kMemLimitPrefix.size()) == kMemLimitPrefix) {
            std::uint64_t mb = 0;
            if (parseWholeNumber(arg.substr(kMemLimitPrefix.size()), mb) && mb > 0 && mb <= kMaxMemLimitMb) {
                options.memLimitMb = mb;
            } else {
                options.problems.push_back("ignored " + std::string(arg) + ": expected a whole number of MB from 1 to " +
                                           std::to_string(kMaxMemLimitMb));
            }
        } else {
            options.problems.push_back("ignored unknown flag " + std::string(arg));
        }
    }
    return options;
}

}  // namespace snx::core
