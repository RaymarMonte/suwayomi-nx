#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Json.hpp"
#include "core/models/ReadProgress.hpp"

// The waiting-save queue's rules and its file (WP-15b, D53). A progress save that fails as the reader closes (the server
// can't be reached at that moment) isn't lost: it waits in progress-queue.json and goes to the server once the server
// answers again, also after a restart, unless the progress on the server changed meanwhile (then the newer server change
// wins). The app's side (the file on the SD card, the rounds that send) is ui/reader/WaitingSaves. Pure: unit-tested and
// fuzzed (tests/fuzz/fuzz_progress_queue.cpp).
namespace snx::core {

// One waiting save: what the reader wanted saved, and what the server had when the reader last knew it (the base).
struct WaitingSave {
    std::string serverUrl;  // the server it belongs to (a round sends only the configured server's)
    std::string user;       // ... and the login's user name ("" without a login)
    int chapterId = 0;
    int mangaId = 0;
    int page = 0;                // lastPageRead to send (page index)
    bool read = false;           // true: also marks the chapter read (false leaves the read flag alone, D44)
    std::optional<double> spot;  // the webtoon strip's spot, sent with the page as the meta note (D49)
    ServerProgress base;         // the server's page, read flag and stamp as the reader last knew them (no note)
    std::string mangaTitle;      // for the log and History's note
    std::string chapterName;
    std::int64_t madeAt = 0;  // when it was made: seconds since 1970 by the console's clock (only for the log)
    int errors = 0;           // answers in a row that were neither "sent" nor "the server is away" (a third drops it)
};

// One entry per chapter of one server and user.
bool sameChapter(const WaitingSave& a, const WaitingSave& b);

// Adds `entry` at the end (the newest). An entry of the same chapter is replaced: the new one keeps the old one's base
// (what the server had before either save) and starts its error count afresh. Then at most `max` entries stay: the oldest
// go. Returns the entries dropped for the cap (for the log).
std::vector<WaitingSave> addWaitingSave(std::vector<WaitingSave>& entries, WaitingSave entry, std::size_t max);

// progress-queue.json, version 1: {"version": 1, "entries": [ {...}, ... ]}, oldest first.
inline constexpr int kProgressQueueVersion = 1;
inline constexpr std::size_t kWaitingTextMaxBytes = 256;  // titles and names are cut to this (S6); longer URLs are refused
struct ProgressQueueLoad {
    std::vector<WaitingSave> entries;
    std::vector<std::string> problems;  // for the log: skipped entries, a newer version, ...
    bool usable = false;                // the text was a queue file (an object with an "entries" list); else try the .bak
};
// Tolerant (S6): a broken entry (no server, no chapter id, a page below 0, a URL over kWaitingTextMaxBytes) is skipped,
// unknown fields are ignored, at most `max` entries are kept (the newest). Never throws.
ProgressQueueLoad loadProgressQueue(std::string_view text, const JsonLimits& limits, std::size_t max);
std::string saveProgressQueue(const std::vector<WaitingSave>& entries);  // one entry per line
// Drops the oldest entries until the file is at most `maxBytes` (RuntimeProfile::progressQueueFileBytes: what the app reads
// back at start, S6). Returns them, for the log.
std::vector<WaitingSave> fitProgressQueue(std::vector<WaitingSave>& entries, std::size_t maxBytes);

// What a round does with an entry, from the server's state now (ChapterState):
// - Already: the server has our page (and read flag, and spot) already: nothing to send.
// - Send: the server is still at the base: nobody changed it since, so ours goes out.
// - ServerMoved: the server's progress changed meanwhile (another device, the web UI): theirs is newer, ours is dropped.
// The reader's take-over (it opens a chapter that has a waiting entry) uses the same rule on fetchChapterPages' answer.
enum class WaitingDecision { Already, Send, ServerMoved };
WaitingDecision decideWaitingSave(const WaitingSave& entry, const ServerProgress& server);
const char* waitingDecisionName(WaitingDecision decision);

// The save to send for an entry (setProgress; with a spot it goes as UpdateStripProgress).
ReadProgress progressOf(const WaitingSave& entry);
// For the log: "page 5, read, stamped 1794534794".
std::string describeServerProgress(const ServerProgress& progress);
std::string describeWaitingSave(const WaitingSave& entry);  // "Alpha / Chapter 3 (chapter 3): page 5, unread"

}  // namespace snx::core
