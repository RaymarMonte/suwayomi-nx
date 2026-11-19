// Fuzz target (SAFETY.md S6, WP-18): any bytes as each of the Switch downloads' three files (manga.json, queue.json,
// server.json). Reading must never crash, and whatever a reader keeps must write and read back unchanged with no problems
// (so a damaged file can't turn into an index or a queue that breaks the next start).
// Seeds: tests/fuzz/seeds/switch-downloads/. Run: tools/build.sh fuzz
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/Json.hpp"
#include "core/SwitchDownloads.hpp"

using namespace snx::core;

namespace {
constexpr std::size_t kMaxEntries = 1000;  // RuntimeProfile::switchQueueMax's default; the fuzzer only needs a cap
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const JsonLimits limits;

    const MangaFileLoad manga = readMangaFile(text, limits);
    if (manga.usable) {
        const std::string saved = writeMangaFile(manga.manga);
        const MangaFileLoad again = readMangaFile(saved, limits);
        if (!again.usable || !again.problems.empty() || again.skipped != 0 || writeMangaFile(again.manga) != saved) {
            std::abort();  // a round trip that changes the index is a bug
        }
    }

    const QueueFileLoad queue = readQueueFile(text, limits, kMaxEntries);
    if (queue.queue.entries.size() > kMaxEntries) {
        std::abort();  // the cap holds whatever the file says
    }
    if (queue.usable) {
        const std::string saved = writeQueueFile(queue.queue);
        const QueueFileLoad again = readQueueFile(saved, limits, kMaxEntries);
        if (!again.usable || !again.problems.empty() || again.queue.entries.size() != queue.queue.entries.size() ||
            writeQueueFile(again.queue) != saved) {
            std::abort();
        }
    }

    if (const auto urls = readServerFile(text, limits)) {
        if (urls->size() > kSwitchUrlsMax) {
            std::abort();
        }
        const auto again = readServerFile(writeServerFile(*urls), limits);
        if (!again || *again != *urls) {
            std::abort();
        }
    }
    return 0;
}
