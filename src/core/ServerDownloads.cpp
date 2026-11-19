#include "core/ServerDownloads.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "core/Json.hpp"

namespace snx::core {

namespace {

// The row's state, or false for one we don't show (FINISHED, or a word we don't know).
bool parseState(const std::string& text, DownloadState& out) {
    if (text == "QUEUED") {
        out = DownloadState::Queued;
    } else if (text == "DOWNLOADING") {
        out = DownloadState::Downloading;
    } else if (text == "ERROR") {
        out = DownloadState::Error;
    } else {
        return false;
    }
    return true;
}

}  // namespace

Result<DownloadQueue> readDownloadQueue(const nlohmann::json& status, std::size_t maxRows) {
    if (!status.is_object()) {
        return Result<DownloadQueue>::fail(ErrorKind::Parse, "downloadStatus is not an object");
    }
    DownloadQueue queue;
    queue.started = json::string(status, "state").value_or("") == "STARTED";
    const nlohmann::json* list = json::array(status, "queue");
    if (!list) {
        return queue;  // tolerant: no rows
    }
    std::vector<DownloadRow> rows;
    for (const nlohmann::json& node : *list) {
        if (!node.is_object()) {
            queue.skipped += 1;
            continue;
        }
        const std::string stateText = json::string(node, "state").value_or("");
        if (stateText == "FINISHED") {
            continue;  // done: it leaves the queue anyway
        }
        DownloadState state = DownloadState::Queued;
        const nlohmann::json* chapter = json::object(node, "chapter");
        const std::optional<int> chapterId = chapter ? json::int32(*chapter, "id") : std::nullopt;
        if (!parseState(stateText, state) || !chapterId) {
            queue.skipped += 1;
            continue;
        }
        switch (state) {
            case DownloadState::Queued: queue.queued += 1; break;
            case DownloadState::Downloading: queue.downloading += 1; break;
            case DownloadState::Error: queue.failed += 1; break;
        }
        if (rows.size() >= maxRows) {
            queue.leftOut += 1;  // counted, not kept (S6)
            continue;
        }
        DownloadRow row;
        row.chapterId = *chapterId;
        row.state = state;
        row.chapterName = json::string(*chapter, "name").value_or("");
        row.sourceOrder = json::int32(*chapter, "sourceOrder").value_or(0);
        if (const nlohmann::json* manga = json::object(node, "manga")) {
            row.mangaId = json::int32(*manga, "id").value_or(0);
            row.mangaTitle = json::string(*manga, "title").value_or("");
            row.thumbnailUrl = json::string(*manga, "thumbnailUrl").value_or("");
        }
        const double progress = json::number(node, "progress").value_or(0);
        row.progress = std::isfinite(progress) ? std::clamp(progress, 0.0, 1.0) : 0.0;
        row.tries = std::max(0, json::int32(node, "tries").value_or(0));
        row.position = json::int32(node, "position").value_or(static_cast<int>(rows.size()));
        rows.push_back(std::move(row));
    }
    // The server lists them in order already; a stable sort keeps that when positions are missing or equal.
    std::stable_sort(rows.begin(), rows.end(),
                     [](const DownloadRow& a, const DownloadRow& b) { return a.position < b.position; });
    queue.rows = std::move(rows);
    return queue;
}

std::vector<int> chaptersThatLeft(const DownloadQueue& before, const DownloadQueue& after) {
    if (after.leftOut > 0) {
        return {};  // a missing row may only have moved past the kept part: report none rather than guess
    }
    std::set<int> still;
    for (const DownloadRow& row : after.rows) {
        still.insert(row.chapterId);
    }
    std::vector<int> left;
    for (const DownloadRow& row : before.rows) {
        if (still.count(row.chapterId) == 0) {
            left.push_back(row.chapterId);
        }
    }
    return left;
}

QueueView queueView(const DownloadQueue& queue, std::int64_t nowMs, std::int64_t startAskedAtMs, int startWaitMs) {
    if (!queue.waiting()) {
        return QueueView::Idle;
    }
    if (queue.started) {
        return QueueView::Running;
    }
    if (startAskedAtMs > 0 && nowMs >= startAskedAtMs && nowMs - startAskedAtMs < startWaitMs) {
        return QueueView::Starting;
    }
    return QueueView::Paused;
}

const char* queueViewName(QueueView view) {
    switch (view) {
        case QueueView::Idle: return "idle";
        case QueueView::Starting: return "starting";
        case QueueView::Running: return "running";
        case QueueView::Paused: return "paused";
    }
    return "idle";
}

std::string percentText(double share) {
    if (!std::isfinite(share)) {
        share = 0;
    }
    // A hair added first, so 0.29 (stored as 0.28999...) still reads 29 %.
    const int percent = static_cast<int>(std::floor(std::clamp(share, 0.0, 1.0) * 100.0 + 1e-6));
    return std::to_string(std::clamp(percent, 0, 100)) + " %";
}

std::vector<int> chaptersToDownload(const std::vector<Chapter>& chapters, DownloadChoice choice, const std::set<int>& skip) {
    std::vector<const Chapter*> order;
    order.reserve(chapters.size());
    for (const Chapter& chapter : chapters) {
        order.push_back(&chapter);
    }
    std::stable_sort(order.begin(), order.end(),
                     [](const Chapter* a, const Chapter* b) { return a->sourceOrder < b->sourceOrder; });
    std::size_t limit = order.size();
    switch (choice) {
        case DownloadChoice::Next1: limit = 1; break;
        case DownloadChoice::Next5: limit = 5; break;
        case DownloadChoice::Next10: limit = 10; break;
        case DownloadChoice::Next25: limit = 25; break;
        case DownloadChoice::Unread:
        case DownloadChoice::All: break;
    }
    const bool readToo = choice == DownloadChoice::All;
    std::vector<int> ids;
    for (const Chapter* chapter : order) {
        if (ids.size() >= limit) {
            break;
        }
        if (skip.count(chapter->id) > 0 || (chapter->isRead && !readToo)) {
            continue;
        }
        ids.push_back(chapter->id);
    }
    return ids;
}

std::set<int> serverSkipIds(const std::vector<Chapter>& chapters, const std::vector<int>& queuedIds) {
    std::set<int> skip(queuedIds.begin(), queuedIds.end());
    for (const Chapter& chapter : chapters) {
        if (chapter.isDownloaded) {
            skip.insert(chapter.id);
        }
    }
    return skip;
}

std::vector<std::vector<int>> idBatches(const std::vector<int>& ids, int batchMax) {
    const std::size_t size = static_cast<std::size_t>(std::max(batchMax, 1));
    std::vector<std::vector<int>> batches;
    for (std::size_t i = 0; i < ids.size(); i += size) {
        batches.emplace_back(ids.begin() + static_cast<std::ptrdiff_t>(i),
                             ids.begin() + static_cast<std::ptrdiff_t>(std::min(ids.size(), i + size)));
    }
    return batches;
}

}  // namespace snx::core
