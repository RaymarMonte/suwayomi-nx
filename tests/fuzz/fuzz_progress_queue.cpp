// Fuzz target (SAFETY.md S6, WP-15b): any bytes as progress-queue.json, the waiting progress saves. Loading must never crash,
// and whatever it loads must save and load back unchanged with no problems (so a damaged file can't turn into a queue that
// breaks the next start, or sends something other than what was saved).
// Seeds: tests/fuzz/seeds/progress-queue/. Run: tools/build.sh fuzz
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/Json.hpp"
#include "core/ProgressQueue.hpp"

using namespace snx::core;

namespace {
constexpr std::size_t kMaxEntries = 100;  // RuntimeProfile::pendingSavesMax's default; the fuzzer only needs a cap
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const JsonLimits limits;
    const ProgressQueueLoad load = loadProgressQueue(text, limits, kMaxEntries);
    if (load.entries.size() > kMaxEntries) {
        std::abort();  // the cap holds whatever the file says
    }
    const std::string saved = saveProgressQueue(load.entries);
    const ProgressQueueLoad again = loadProgressQueue(saved, limits, kMaxEntries);
    if (!again.usable || !again.problems.empty() || again.entries.size() != load.entries.size() ||
        saveProgressQueue(again.entries) != saved) {
        std::abort();  // a round trip that changes the waiting saves is a bug
    }
    return 0;
}
