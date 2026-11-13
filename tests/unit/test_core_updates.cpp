// WP-15: the rules of the Updates and History tabs (core/UpdatesHistory.hpp).
#include <set>
#include <vector>

#include <doctest/doctest.h>

#include "core/UpdatesHistory.hpp"

using namespace snx::core;

namespace {

ChapterEntry entry(int chapterId, int mangaId, std::int64_t fetchedAt, std::int64_t inLibraryAt) {
    ChapterEntry e;
    e.chapter.id = chapterId;
    e.chapter.mangaId = mangaId;
    e.chapter.fetchedAt = fetchedAt;
    e.manga.id = mangaId;
    e.manga.inLibraryAt = inLibraryAt;
    return e;
}

constexpr std::int64_t kNov13 = 1763035200;  // 2025-11-13 12:00:00 UTC (a Thursday)

}  // namespace

TEST_SUITE("core") {
    TEST_CASE("Updates: a chapter is new when it was fetched after its manga joined the library (equal = old)") {
        CHECK(isNewChapter(entry(1, 1, 1000, 999)));
        CHECK_FALSE(isNewChapter(entry(1, 1, 1000, 1000)));  // the same second: it came with the manga
        CHECK_FALSE(isNewChapter(entry(1, 1, 999, 1000)));   // a moment before: it came with the manga
        CHECK_FALSE(isNewChapter(entry(1, 1, 0, 0)));        // unknown time: never new
        CHECK(isNewChapter(entry(1, 1, 5, 0)));              // a manga without inLibraryAt: any known fetch is new
    }

    TEST_CASE("Updates: the window ends 90 days before the newest chapter, and the list stops there") {
        CHECK(updatesWindowStart(kNov13) == kNov13 - 90 * 86400);
        UpdatesFilter filter;
        const std::int64_t newest = kNov13;
        const std::vector<ChapterEntry> page = {
            entry(10, 1, newest, 100),                     // new
            entry(11, 2, newest - 10, newest),             // came with its manga: hidden
            entry(12, 1, newest - 90 * 86400, 100),        // exactly at the window's start: still shown
            entry(13, 1, newest - 90 * 86400 - 1, 100),    // older: the end
            entry(14, 1, newest - 100 * 86400, 100),
        };
        const UpdatesPage out = filterUpdates(page, filter);
        REQUIRE(out.rows.size() == 2);
        CHECK(out.rows[0].chapter.id == 10);
        CHECK(out.rows[1].chapter.id == 12);
        CHECK(out.hidden == 1);
        CHECK(out.windowEnded);
        REQUIRE(filter.windowStart.has_value());
        CHECK(*filter.windowStart == newest - 90 * 86400);
    }

    TEST_CASE("Updates: a chapter on two pages shows once, and the window comes from the first page only") {
        UpdatesFilter filter;
        const UpdatesPage first = filterUpdates({entry(10, 1, 5000, 1), entry(11, 1, 4000, 1)}, filter);
        CHECK(first.rows.size() == 2);
        CHECK_FALSE(first.windowEnded);
        // The next page starts with a chapter the first already had (a chapter moved between the two requests).
        const UpdatesPage second = filterUpdates({entry(11, 1, 4000, 1), entry(12, 1, 3000, 1)}, filter);
        REQUIRE(second.rows.size() == 1);
        CHECK(second.rows[0].chapter.id == 12);
        CHECK(second.hidden == 1);
        CHECK(*filter.windowStart == updatesWindowStart(5000));
    }

    TEST_CASE("History: one row per manga, its first chapter, across pages") {
        std::set<int> seen;
        const auto first = historyRows({entry(5, 1, 0, 0), entry(4, 1, 0, 0), entry(9, 2, 0, 0)}, seen);
        REQUIRE(first.size() == 2);
        CHECK(first[0].chapter.id == 5);
        CHECK(first[1].chapter.id == 9);
        const auto second = historyRows({entry(3, 1, 0, 0), entry(20, 3, 0, 0), entry(21, 3, 0, 0)}, seen);
        REQUIRE(second.size() == 1);
        CHECK(second[0].chapter.id == 20);
    }

    TEST_CASE("Days: the local day around midnight and with an offset") {
        const std::int64_t midnightUtc = 1763078400;  // 2025-11-14 00:00:00 UTC
        CHECK(dayKey(midnightUtc - 1, 0) + 1 == dayKey(midnightUtc, 0));
        // UTC+1: 23:30 UTC on the 13th is already the 14th.
        CHECK(dayKey(midnightUtc - 1800, 3600) == dayKey(midnightUtc, 0));
        // UTC-5: 03:00 UTC on the 14th is still the 13th.
        CHECK(dayKey(midnightUtc + 3 * 3600, -5 * 3600) == dayKey(midnightUtc - 1, 0));
        // Before 1970 (floor division).
        CHECK(dayKey(-1, 0) == -1);
        const CivilDate date = civilFromDay(dayKey(kNov13, 0));
        CHECK(date.year == 2025);
        CHECK(date.month == 11);
        CHECK(date.day == 13);
        CHECK(date.weekday == 4);  // Thursday
        CHECK(civilFromDay(0).weekday == 4);
        const CivilDate leap = civilFromDay(dayKey(951782400, 0));  // 2000-02-29
        CHECK(leap.month == 2);
        CHECK(leap.day == 29);
    }

    TEST_CASE("Days: Today, Yesterday (across a month end), a date, a date of another year") {
        const std::int64_t dec1 = dayKey(1764590400, 0);  // 2025-12-01
        CHECK(dayLabel(dec1, dec1).kind == DayLabel::Kind::Today);
        const DayLabel yesterday = dayLabel(dec1 - 1, dec1);  // 30 Nov
        CHECK(yesterday.kind == DayLabel::Kind::Yesterday);
        CHECK(yesterday.date.month == 11);
        CHECK(yesterday.date.day == 30);
        const DayLabel date = dayLabel(dayKey(kNov13, 0), dec1);
        CHECK(date.kind == DayLabel::Kind::Date);
        CHECK_FALSE(date.withYear);
        CHECK(formatDate(date) == "Thu 13 Nov");
        const std::int64_t jan2 = dayKey(1767355200, 0);  // 2026-01-02
        const DayLabel lastYear = dayLabel(dayKey(kNov13, 0), jan2);
        CHECK(lastYear.withYear);
        CHECK(formatDate(lastYear) == "Thu 13 Nov 2025");
        // Yesterday across a year end still says Yesterday; the day before it is a date with its year.
        const std::int64_t jan1 = jan2 - 1;
        CHECK(dayLabel(jan1 - 1, jan1).kind == DayLabel::Kind::Yesterday);
        CHECK(dayLabel(jan1 - 2, jan1).withYear);
        CHECK(dayLabel(jan1 - 2, jan1).kind == DayLabel::Kind::Date);
    }

    TEST_CASE("Days: the last update's date and time, local, with the year only when it differs") {
        const std::int64_t at = 1763044320;  // 2025-11-13 14:32:00 UTC
        CHECK(formatDateTime(at, 0, at) == "13 Nov, 14:32");
        CHECK(formatDateTime(at, 3600, at) == "13 Nov, 15:32");
        CHECK(formatDateTime(at, 10 * 3600, at) == "14 Nov, 00:32");
        CHECK(formatDateTime(at, 0, 1767355200) == "13 Nov 2025, 14:32");
    }

    TEST_CASE("Days: groups start where the local day changes") {
        const std::int64_t day = 86400;
        const std::vector<std::int64_t> times = {10 * day + 500, 10 * day + 100, 9 * day + 80000, 9 * day, 7 * day};
        const auto groups = dayGroups(times, 0);
        REQUIRE(groups.size() == 3);
        CHECK(groups[0].first == 0);
        CHECK(groups[0].day == 10);
        CHECK(groups[1].first == 2);
        CHECK(groups[1].day == 9);
        CHECK(groups[2].first == 4);
        // With an offset 9 * day + 80000 moves into day 10.
        const auto shifted = dayGroups(times, 7200);
        CHECK(shifted[1].first == 3);
        CHECK(dayGroups({}, 0).empty());
    }

    TEST_CASE("Library update: the stale first answer, then running, then done") {
        LibraryUpdateStatus status;
        status.lastUpdateMs = 1000;  // the previous update's
        status.running = false;
        status.total = 40;  // the previous job's numbers: looks finished
        status.finished = 40;
        LibraryUpdateWatch watch = LibraryUpdateWatch::started(1000, 0, 10000);
        CHECK(watch.next(status, 100) == UpdatePhase::Starting);  // the old timestamp: not ours yet
        status.lastUpdateMs = 2000;
        status.running = true;
        status.finished = 3;
        CHECK(watch.next(status, 1100) == UpdatePhase::Running);
        status.running = false;
        status.finished = 40;
        CHECK(watch.next(status, 2100) == UpdatePhase::Done);
        // Final: a later poll keeps it.
        status.running = true;
        CHECK(watch.next(status, 3100) == UpdatePhase::Done);
        CHECK_FALSE(watch.stopped());
    }

    TEST_CASE("Library update: every manga skipped (the timestamp moves, nothing runs) is done at once") {
        LibraryUpdateStatus status;
        status.lastUpdateMs = 5000;
        status.skippedManga = 156;
        LibraryUpdateWatch watch = LibraryUpdateWatch::started(4000, 0, 10000);
        CHECK(watch.next(status, 50) == UpdatePhase::Done);
    }

    TEST_CASE("Library update: nothing seen within the start wait is 'nothing to update'") {
        LibraryUpdateStatus status;
        status.lastUpdateMs = 4000;
        LibraryUpdateWatch watch = LibraryUpdateWatch::started(4000, 1000, 10000);
        CHECK(watch.next(status, 5000) == UpdatePhase::Starting);
        CHECK(watch.next(status, 10999) == UpdatePhase::Starting);
        CHECK(watch.next(status, 11000) == UpdatePhase::NothingToUpdate);
        status.lastUpdateMs = 9000;
        CHECK(watch.next(status, 12000) == UpdatePhase::NothingToUpdate);  // final
    }

    TEST_CASE("Library update: stopped, the same done rule; somebody else's update ends when nothing runs") {
        LibraryUpdateStatus status;
        status.lastUpdateMs = 2;
        status.running = true;
        LibraryUpdateWatch watch = LibraryUpdateWatch::started(1, 0, 10000);
        CHECK(watch.next(status, 10) == UpdatePhase::Running);
        watch.stopping();
        CHECK(watch.next(status, 20) == UpdatePhase::Running);
        status.running = false;
        CHECK(watch.next(status, 30) == UpdatePhase::Done);
        CHECK(watch.stopped());

        LibraryUpdateWatch other = LibraryUpdateWatch::alreadyRunning();
        CHECK(other.phase() == UpdatePhase::Running);
        status.running = true;
        CHECK(other.next(status, 0) == UpdatePhase::Running);
        status.running = false;
        CHECK(other.next(status, 99999) == UpdatePhase::Done);
    }
}
