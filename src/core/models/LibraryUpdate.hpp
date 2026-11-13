#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace snx::core {

// What the server says about its library update (WP-15, the LibraryUpdateStatus document; SUWAYOMI-API.md "Updates,
// history and the library update"). The counts are the running job's, or the last job's once it has ended.
struct LibraryUpdateStatus {
    bool running = false;       // jobsInfo.isRunning
    int total = 0;              // jobsInfo.totalJobs: manga the update looks at
    int finished = 0;           // jobsInfo.finishedJobs
    int skippedManga = 0;       // jobsInfo.skippedMangasCount: left out by the server's update settings
    int skippedCategories = 0;  // jobsInfo.skippedCategoriesCount
    // lastUpdateTimestamp.timestamp: when the last update started (milliseconds since 1970, UTC; 0 = never). It changes
    // when an update starts and stays the same while it runs.
    std::int64_t lastUpdateMs = 0;
    // Only when the query asked for mangaUpdates: the titles of the manga whose update failed (status FAILED).
    std::vector<std::string> failed;
};

}  // namespace snx::core
