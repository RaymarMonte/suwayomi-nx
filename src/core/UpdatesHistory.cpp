#include "core/UpdatesHistory.hpp"

#include <cstdio>

namespace snx::core {

namespace {

const char* const kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q -= 1;
    }
    return q;
}

}  // namespace

bool isNewChapter(const ChapterEntry& entry) {
    return entry.chapter.fetchedAt > 0 && entry.chapter.fetchedAt > entry.manga.inLibraryAt;
}

std::int64_t updatesWindowStart(std::int64_t newestFetchedAt) {
    return newestFetchedAt - kUpdatesWindowSeconds;
}

UpdatesPage filterUpdates(const std::vector<ChapterEntry>& nodes, UpdatesFilter& filter) {
    UpdatesPage page;
    for (const ChapterEntry& entry : nodes) {
        if (!filter.windowStart) {
            filter.windowStart = updatesWindowStart(entry.chapter.fetchedAt);
        }
        if (entry.chapter.fetchedAt < *filter.windowStart) {
            page.windowEnded = true;
            break;
        }
        if (!isNewChapter(entry) || !filter.seen.insert(entry.chapter.id).second) {
            page.hidden += 1;
            continue;
        }
        page.rows.push_back(entry);
    }
    return page;
}

std::vector<ChapterEntry> historyRows(const std::vector<ChapterEntry>& nodes, std::set<int>& seenManga) {
    std::vector<ChapterEntry> rows;
    for (const ChapterEntry& entry : nodes) {
        if (seenManga.insert(entry.manga.id).second) {
            rows.push_back(entry);
        }
    }
    return rows;
}

std::int64_t dayKey(std::int64_t utcSeconds, std::int64_t utcOffsetSeconds) {
    return floorDiv(utcSeconds + utcOffsetSeconds, kSecondsPerDay);
}

CivilDate civilFromDay(std::int64_t day) {
    // Howard Hinnant's days-to-civil algorithm (public domain, "chrono-Compatible Low-Level Date Algorithms").
    const std::int64_t z = day + 719468;
    const std::int64_t era = floorDiv(z, 146097);
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    CivilDate date;
    date.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    date.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    date.year = static_cast<int>(yoe + era * 400 + (date.month <= 2 ? 1 : 0));
    const std::int64_t weekday = (day + 4) % 7;
    date.weekday = static_cast<int>(weekday < 0 ? weekday + 7 : weekday);
    return date;
}

DayLabel dayLabel(std::int64_t day, std::int64_t today) {
    DayLabel label;
    label.date = civilFromDay(day);
    if (day == today) {
        label.kind = DayLabel::Kind::Today;
    } else if (day == today - 1) {
        label.kind = DayLabel::Kind::Yesterday;
    }
    label.withYear = label.date.year != civilFromDay(today).year;
    return label;
}

std::string formatDate(const DayLabel& label) {
    char text[32];
    if (label.withYear) {
        std::snprintf(text, sizeof text, "%s %d %s %d", kWeekdays[label.date.weekday], label.date.day,
                      kMonths[label.date.month - 1], label.date.year);
    } else {
        std::snprintf(text, sizeof text, "%s %d %s", kWeekdays[label.date.weekday], label.date.day,
                      kMonths[label.date.month - 1]);
    }
    return text;
}

std::string formatDateTime(std::int64_t utcSeconds, std::int64_t utcOffsetSeconds, std::int64_t nowSeconds) {
    const std::int64_t day = dayKey(utcSeconds, utcOffsetSeconds);
    const CivilDate date = civilFromDay(day);
    const std::int64_t secondOfDay = utcSeconds + utcOffsetSeconds - day * kSecondsPerDay;
    const int hour = static_cast<int>(secondOfDay / 3600);
    const int minute = static_cast<int>((secondOfDay % 3600) / 60);
    const bool withYear = date.year != civilFromDay(dayKey(nowSeconds, utcOffsetSeconds)).year;
    char text[40];
    if (withYear) {
        std::snprintf(text, sizeof text, "%d %s %d, %02d:%02d", date.day, kMonths[date.month - 1], date.year, hour,
                      minute);
    } else {
        std::snprintf(text, sizeof text, "%d %s, %02d:%02d", date.day, kMonths[date.month - 1], hour, minute);
    }
    return text;
}

std::vector<DayGroup> dayGroups(const std::vector<std::int64_t>& times, std::int64_t utcOffsetSeconds) {
    std::vector<DayGroup> groups;
    for (std::size_t i = 0; i < times.size(); ++i) {
        const std::int64_t day = dayKey(times[i], utcOffsetSeconds);
        if (groups.empty() || groups.back().day != day) {
            groups.push_back({i, day});
        }
    }
    return groups;
}

LibraryUpdateWatch LibraryUpdateWatch::started(std::int64_t beforeMs, std::int64_t nowMs, int startWaitMs) {
    LibraryUpdateWatch watch;
    watch.before_ = beforeMs;
    watch.startedAt_ = nowMs;
    watch.startWaitMs_ = startWaitMs;
    return watch;
}

LibraryUpdateWatch LibraryUpdateWatch::alreadyRunning() {
    LibraryUpdateWatch watch;
    watch.phase_ = UpdatePhase::Running;
    return watch;
}

UpdatePhase LibraryUpdateWatch::next(const LibraryUpdateStatus& status, std::int64_t nowMs) {
    if (phase_ == UpdatePhase::Done || phase_ == UpdatePhase::NothingToUpdate) {
        return phase_;
    }
    if (!before_) {
        // Somebody else's update: it ends when nothing runs.
        phase_ = status.running ? UpdatePhase::Running : UpdatePhase::Done;
        return phase_;
    }
    const bool moved = status.lastUpdateMs != *before_;
    if (moved && !status.running) {
        phase_ = UpdatePhase::Done;
    } else if (status.running) {
        // Ours (moved), or one that was already running when we asked (the same timestamp): show its progress either way.
        phase_ = UpdatePhase::Running;
    } else if (nowMs - startedAt_ >= startWaitMs_) {
        phase_ = UpdatePhase::NothingToUpdate;
    } else {
        phase_ = UpdatePhase::Starting;
    }
    return phase_;
}

}  // namespace snx::core
