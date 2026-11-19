#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Result.hpp"
#include "core/models/Chapter.hpp"

// Downloads on the server's own disk (WP-17, D58; SUWAYOMI-API.md "Server downloads"): the queue as the server sends it,
// what the Server downloads tab shows for it, and which chapters a bulk choice ("Next 5", "Unread", ...) queues.
// Nothing here is stored on the Switch (that is WP-18).
namespace snx::core {

// A row's state. The server also knows FINISHED, but a finished chapter leaves the queue at once (it was never seen), so
// such a row is left out like a finished one.
enum class DownloadState { Queued, Downloading, Error };

// One chapter in the server's queue.
struct DownloadRow {
    int chapterId = 0;
    std::string chapterName;  // "Chapter 3"
    int sourceOrder = 0;
    int mangaId = 0;  // 0 when the answer had no readable manga part
    std::string mangaTitle;
    std::string thumbnailUrl;  // relative to the server, may be empty
    DownloadState state = DownloadState::Queued;
    double progress = 0;  // pages done / pages, 0..1 (0 while the server loads the page list)
    int tries = 0;        // 3 for a row that failed
    int position = 0;     // 0-based place in the whole queue
};

// The queue as the server sent it (the downloadStatus object).
struct DownloadQueue {
    bool started = false;           // the downloader runs (STARTED); STOPPED: paused, or nothing to do
    std::vector<DownloadRow> rows;  // the server's order, at most the limit given to readDownloadQueue
    int leftOut = 0;                // good rows past that limit (the tab says "and 240 more")
    int skipped = 0;                // broken rows (no chapter id, an unknown state), never shown
    // Over every good row, the ones left out too.
    int queued = 0;
    int downloading = 0;
    int failed = 0;

    int total() const { return static_cast<int>(rows.size()) + leftOut; }  // good rows on the server
    bool waiting() const { return queued + downloading > 0; }  // something the downloader can still download
};

// The downloadStatus object ({state, queue: [...]}) -> the queue. Tolerant: a broken row is skipped and counted, a missing
// field keeps its default, rows past maxRows are only counted. Fails (Parse) only when `status` isn't an object.
Result<DownloadQueue> readDownloadQueue(const nlohmann::json& status, std::size_t maxRows);

// The chapters whose rows are in `before` but not in `after` (ids, `before`'s order). None when `after` left rows out
// (a missing row may only have moved past the kept part).
std::vector<int> chaptersThatLeft(const DownloadQueue& before, const DownloadQueue& after);

// What the Server downloads tab shows for a queue.
enum class QueueView {
    Idle,      // nothing waits: the queue is empty, or holds only failed rows ("Stopped: 2 failed")
    Starting,  // STOPPED with rows waiting, soon after our own enqueue / retry / resume (its answer still says STOPPED)
    Running,   // STARTED with rows waiting
    Paused,    // STOPPED with rows waiting, otherwise
};
// startAskedAtMs: when we last enqueued / retried / resumed (0 = never), by the app's clock. startWaitMs =
// RuntimeProfile::serverDownloadStartWaitMs.
QueueView queueView(const DownloadQueue& queue, std::int64_t nowMs, std::int64_t startAskedAtMs, int startWaitMs);
const char* queueViewName(QueueView view);  // "idle", "starting", "running", "paused" (the test event)

// A share as whole percent, rounded down (0.999 -> "99 %", so 100 % means really done), within 0..100.
std::string percentText(double share);

// The details screen's "Download on server..." choices (the web UI's list).
enum class DownloadChoice { Next1, Next5, Next10, Next25, Unread, All };
// The chapters a choice queues, as ids in reading order (oldest first, by sourceOrder): not downloaded on the server and
// not in `queuedIds`. Next N and Unread take unread chapters only, All read ones too; Next N takes the first N of them.
std::vector<int> chaptersToDownload(const std::vector<Chapter>& chapters, DownloadChoice choice,
                                    const std::vector<int>& queuedIds);

// Ids in pieces of at most `batchMax` (RuntimeProfile::serverDownloadBatchMax, at least 1), in their order.
std::vector<std::vector<int>> idBatches(const std::vector<int>& ids, int batchMax);

}  // namespace snx::core
