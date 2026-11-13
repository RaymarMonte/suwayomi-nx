#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/models/ChapterEntry.hpp"
#include "core/models/LibraryUpdate.hpp"

// The rules of the Updates and History tabs (WP-15), pure so they are unit-tested (tests/unit/test_core_updates.cpp).
// Times: the server's fetchedAt / lastReadAt / inLibraryAt are seconds, lastUpdateTimestamp milliseconds (SUWAYOMI-API.md).
namespace snx::core {

inline constexpr std::int64_t kSecondsPerDay = 24 * 60 * 60;
// Updates shows the chapters found in the last 90 days (counted back from the newest one, so the console's clock
// doesn't matter).
inline constexpr std::int64_t kUpdatesWindowSeconds = 90 * kSecondsPerDay;

// --- Updates ---

// Mihon's "new": the server found the chapter after the manga was in the library (fetchedAt later than inLibraryAt). The
// chapters a manga came with when it was added are fetched a moment before or in the same second, so equal = old. An
// unknown fetchedAt (0) is never new.
bool isNewChapter(const ChapterEntry& entry);

// The oldest fetchedAt the list shows: 90 days before the newest chapter's.
std::int64_t updatesWindowStart(std::int64_t newestFetchedAt);

// What one page of the Updates query adds to the list. The list is newest first (fetchedAt descending).
struct UpdatesFilter {
    std::optional<std::int64_t> windowStart;  // set from the very first chapter the server sent
    std::set<int> seen;                       // chapter ids already in the list (a chapter on two pages shows once)
};
struct UpdatesPage {
    std::vector<ChapterEntry> rows;  // the new chapters of this page, in the server's order
    int hidden = 0;                  // chapters of this page that aren't new (or were already shown)
    bool windowEnded = false;        // a chapter older than the window came: nothing after it can show
};
UpdatesPage filterUpdates(const std::vector<ChapterEntry>& nodes, UpdatesFilter& filter);

// --- History ---

// One row per manga: the first chapter the server sends for a manga (the one read most recently) and none after it,
// across pages too. `seenManga` keeps the manga already shown between pages.
std::vector<ChapterEntry> historyRows(const std::vector<ChapterEntry>& nodes, std::set<int>& seenManga);

// --- Days ---

// The local day of a time stamp: whole days since 1970-01-01 in a time zone `utcOffsetSeconds` east of UTC (floor
// division, so times before 1970 work too).
std::int64_t dayKey(std::int64_t utcSeconds, std::int64_t utcOffsetSeconds);

struct CivilDate {
    int year = 1970;
    int month = 1;    // 1..12
    int day = 1;      // 1..31
    int weekday = 4;  // 0 = Sunday .. 6 = Saturday (1970-01-01 was a Thursday)
};
CivilDate civilFromDay(std::int64_t day);

// How a day header reads: "Today", "Yesterday" (by the console's clock: `today` = dayKey of now), else the date, with the
// year when it isn't today's year.
struct DayLabel {
    enum class Kind { Today, Yesterday, Date };
    Kind kind = Kind::Date;
    CivilDate date;
    bool withYear = false;
};
DayLabel dayLabel(std::int64_t day, std::int64_t today);

// The date in English: "Fri 13 Nov", with the year "Fri 13 Nov 2025". (The UI turns Today / Yesterday into its own words.)
std::string formatDate(const DayLabel& label);

// "13 Nov, 14:32" (with the year when it isn't today's: "13 Nov 2025, 14:32"): the Updates tab's "Last updated". The
// time is local (`utcOffsetSeconds`); `nowSeconds` decides the year.
std::string formatDateTime(std::int64_t utcSeconds, std::int64_t utcOffsetSeconds, std::int64_t nowSeconds);

// Where each day starts in a list: the row index and the day, in list order. `times` are the rows' seconds (fetchedAt or
// lastReadAt) in list order. A day that comes back later in the list (it can't in a sorted list) starts a new group.
struct DayGroup {
    std::size_t first = 0;
    std::int64_t day = 0;
};
std::vector<DayGroup> dayGroups(const std::vector<std::int64_t>& times, std::int64_t utcOffsetSeconds);

// --- The library update ---

// Where a library update stands, worked out from the status polls. The server's own answer to updateLibrary is the
// previous job's status, so it is never used: the update has started once lastUpdateTimestamp differs from the one read
// before starting, and it is done when that is so and nothing runs (SUWAYOMI-API.md).
enum class UpdatePhase {
    Starting,         // asked for, not seen running yet
    Running,          // the server counts up (finished / total)
    Done,             // it ran and ended (or was stopped)
    NothingToUpdate,  // after startWaitMs the timestamp hadn't moved and nothing ran
};

class LibraryUpdateWatch {
  public:
    // An update this app started: `beforeMs` = lastUpdateTimestamp read just before asking, `nowMs` = the app's clock
    // when it asked, `startWaitMs` = RuntimeProfile::libraryUpdateStartWaitMs.
    static LibraryUpdateWatch started(std::int64_t beforeMs, std::int64_t nowMs, int startWaitMs);
    // An update that was already running when the app looked (the web UI's, the server's schedule): done once nothing
    // runs.
    static LibraryUpdateWatch alreadyRunning();

    // The next status poll. Done and NothingToUpdate are final: later polls keep them.
    UpdatePhase next(const LibraryUpdateStatus& status, std::int64_t nowMs);
    void stopping() { stopped_ = true; }  // updateStop was sent: the same done rule; stopped() tells the summary
    bool stopped() const { return stopped_; }
    UpdatePhase phase() const { return phase_; }

  private:
    std::optional<std::int64_t> before_;  // none: an update somebody else started
    std::int64_t startedAt_ = 0;
    int startWaitMs_ = 0;
    bool stopped_ = false;
    UpdatePhase phase_ = UpdatePhase::Starting;
};

}  // namespace snx::core
