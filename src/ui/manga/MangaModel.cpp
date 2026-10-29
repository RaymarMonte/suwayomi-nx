#include "ui/manga/MangaModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

namespace snx::ui {

std::string_view chapterSortName(ChapterSort sort) {
    return sort == ChapterSort::NewestFirst ? "newest" : "oldest";
}

std::string_view chapterFilterName(ChapterFilter filter) {
    switch (filter) {
        case ChapterFilter::All:
            break;
        case ChapterFilter::Unread:
            return "unread";
        case ChapterFilter::Bookmarked:
            return "bookmarked";
    }
    return "all";
}

ContinuePoint continueReading(const std::vector<core::Chapter>& chapters) {
    ContinuePoint point;
    if (chapters.empty()) {
        return point;
    }
    // Reading order: oldest first (sourceOrder), then id, so the answer never depends on the list's order.
    std::vector<const core::Chapter*> order;
    order.reserve(chapters.size());
    for (const core::Chapter& chapter : chapters) {
        order.push_back(&chapter);
    }
    std::sort(order.begin(), order.end(), [](const core::Chapter* a, const core::Chapter* b) {
        return a->sourceOrder != b->sourceOrder ? a->sourceOrder < b->sourceOrder : a->id < b->id;
    });
    const auto at = [&point](const core::Chapter& chapter, ContinuePoint::Kind kind) {
        point.kind = kind;
        point.chapterId = chapter.id;
        point.page = chapter.isRead ? 0 : std::max(0, chapter.lastPageRead);
        return point;
    };

    // 1. The chapter read most recently (">=": a tie goes to the later chapter). Only read chapters and ones with progress
    // count: marking a chapter unread (lastPageRead 0) stamps it too.
    std::size_t anchor = order.size();
    for (std::size_t i = 0; i < order.size(); ++i) {
        const bool position = order[i]->isRead || order[i]->lastPageRead > 0;
        const bool newer = anchor == order.size() || order[i]->lastReadAt >= order[anchor]->lastReadAt;
        if (position && order[i]->lastReadAt > 0 && newer) {
            anchor = i;
        }
    }
    if (anchor < order.size() && !order[anchor]->isRead) {
        return at(*order[anchor], ContinuePoint::Kind::Continue);
    }
    // Without time stamps: the last read chapter in reading order.
    if (anchor == order.size()) {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i]->isRead) {
                anchor = i;
            }
        }
    }
    // 3. Nothing read: a chapter with progress, else the start.
    if (anchor == order.size()) {
        for (const core::Chapter* chapter : order) {
            if (chapter->lastPageRead > 0) {
                return at(*chapter, ContinuePoint::Kind::Continue);
            }
        }
        return at(*order.front(), ContinuePoint::Kind::Start);
    }
    // 2. The first unread chapter after it, else the first unread one from the start.
    for (std::size_t i = anchor + 1; i < order.size(); ++i) {
        if (!order[i]->isRead) {
            return at(*order[i], ContinuePoint::Kind::Continue);
        }
    }
    for (std::size_t i = 0; i < anchor; ++i) {
        if (!order[i]->isRead) {
            return at(*order[i], ContinuePoint::Kind::Continue);
        }
    }
    point.kind = ContinuePoint::Kind::AllRead;
    return point;
}

std::string chapterDate(std::int64_t milliseconds) {
    if (milliseconds <= 0) {
        return {};
    }
    // Days since 1970-01-01 -> year, month, day (Howard Hinnant's civil_from_days, public domain).
    const std::int64_t days = milliseconds / 86400000 + 719468;
    const std::int64_t era = days / 146097;
    const std::int64_t dayOfEra = days - era * 146097;
    const std::int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const std::int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const std::int64_t monthIndex = (5 * dayOfYear + 2) / 153;
    const std::int64_t day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    const std::int64_t month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    const std::int64_t year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
    if (year > 9999) {
        return {};
    }
    char text[40];  // room for any int, so the compiler sees nothing can be cut off
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", static_cast<int>(year), static_cast<int>(month),
                  static_cast<int>(day));
    return text;
}

std::string chapterNumberText(double number) {
    if (!(number >= 0) || number > 1e9) {
        return {};  // unknown (-1), not a number, or nonsense
    }
    char text[32];
    std::snprintf(text, sizeof text, "%.2f", number);
    std::string out = text;
    while (out.back() == '0') {
        out.pop_back();
    }
    if (out.back() == '.') {
        out.pop_back();
    }
    return out;
}

MangaModel::MangaModel(api::SuwayomiClient& client, core::Manga manga, std::size_t maxChapters)
    : client_(client), manga_(std::move(manga)), maxChapters_(std::max<std::size_t>(1, maxChapters)) {}

MangaModel::~MangaModel() {
    loadCancel_.cancel();
    actionCancel_.cancel();
}

void MangaModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void MangaModel::changed() {
    if (listener_) {
        listener_();
    }
}

void MangaModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    load();
}

void MangaModel::retry() {
    started_ = true;
    if (state_ == State::Ready && moreFailed_ && !busy_) {
        moreFailed_ = false;  // carry on where the list stopped
        changed();
        loadPage(false);
        return;
    }
    load();
}

void MangaModel::load() {
    loadCancel_.cancel();
    loadCancel_ = net::CancelToken();
    state_ = State::Loading;
    detailsLoaded_ = false;
    loading_ = Loading();
    loaded_.clear();
    items_.clear();
    ++listRevision_;
    total_ = 0;
    firstPage_ = false;
    complete_ = false;
    moreFailed_ = false;
    changed();

    const net::CancelToken token = loadCancel_;
    client_.manga(manga_.id, token).then([this, token](const api::ApiResult<core::Manga>& result) {
        if (token.cancelled()) {
            return;  // the model may be gone
        }
        onDetails(result);
    });
    if (state_ == State::Failed) {
        return;  // the details failed at once (tests: the answer comes inside the call)
    }
    loadPage(false);
}

void MangaModel::onDetails(const api::ApiResult<core::Manga>& result) {
    if (!result.ok()) {
        fail(result.error());
        return;
    }
    manga_ = result.value();
    detailsLoaded_ = true;
    updateUnreadCount();
    updateState();
    changed();
}

void MangaModel::loadPage(bool replace) {
    const net::CancelToken token = loadCancel_;
    client_.chapters(manga_.id, loading_.nextPage, token)
        .then([this, token, replace](const api::ApiResult<core::NodeList<core::Chapter>>& result) {
            if (token.cancelled()) {
                return;
            }
            onPage(replace, result);
        });
}

void MangaModel::onPage(bool replace, const api::ApiResult<core::NodeList<core::Chapter>>& result) {
    if (!result.ok()) {
        if (replace) {
            finishReload(false, result.error());
        } else if (!firstPage_) {
            fail(result.error());
        } else {
            moreFailed_ = true;  // keep what is loaded
            changed();
        }
        return;
    }
    const core::NodeList<core::Chapter>& page = result.value();
    // The first load fills the list as it arrives; a reload collects everything first.
    std::vector<core::Chapter>& target = replace ? loading_.chapters : loaded_;
    for (const core::Chapter& chapter : page.nodes) {
        if (target.size() >= maxChapters_) {
            break;
        }
        if (loading_.ids.insert(chapter.id).second) {
            target.push_back(chapter);
        }
    }
    loading_.total = page.totalCount;
    loading_.nextPage += 1;
    loading_.started = true;
    // An empty page that claims there is more would make the list ask forever.
    const bool done = !page.hasNextPage || page.nodes.empty() || target.size() >= maxChapters_;
    // The next page is asked for last: its answer may come inside the call (tests), and must find this page saved.
    if (replace) {
        if (!done) {
            loadPage(true);
            return;
        }
        loaded_ = std::move(loading_.chapters);
        total_ = loading_.total;
        complete_ = true;
        moreFailed_ = false;
        finishReload(true, {});
        return;
    }
    total_ = loading_.total;
    firstPage_ = true;
    complete_ = done;
    rebuildItems(false);
    updateUnreadCount();
    updateState();
    changed();
    if (!done) {
        loadPage(false);
    }
}

void MangaModel::reloadChapters() {
    if (busy_ || (state_ != State::Ready && state_ != State::Empty)) {
        return;
    }
    busy_ = true;
    changed();
    beginReload();
}

void MangaModel::beginReload() {
    loadCancel_.cancel();  // the first load may still be loading pages: the reload takes over
    loadCancel_ = net::CancelToken();
    loading_ = Loading();
    loadPage(true);
}

void MangaModel::finishReload(bool ok, const api::ApiError& error) {
    busy_ = false;
    refreshing_ = false;
    if (ok) {
        firstPage_ = true;
        rebuildItems(true);
        updateUnreadCount();
        updateState();
    } else {
        actionError_ = error;
    }
    changed();
}

void MangaModel::refreshFromSource() {
    if (busy_ || (state_ != State::Ready && state_ != State::Empty)) {
        return;
    }
    busy_ = true;
    refreshing_ = true;
    changed();
    const net::CancelToken token = actionCancel_;
    client_.refreshManga(manga_.id, token).then([this, token](const api::ApiResult<core::Manga>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            busy_ = false;
            refreshing_ = false;
            actionError_ = result.error();
            changed();
            return;
        }
        manga_ = result.value();
        detailsLoaded_ = true;
        changed();
        beginReload();  // busy until the new chapter list is in
    });
}

void MangaModel::setSort(ChapterSort sort) {
    if (sort == sort_) {
        return;
    }
    sort_ = sort;
    if (firstPage_) {
        rebuildItems(false);
    }
    changed();
}

void MangaModel::setFilter(ChapterFilter filter) {
    if (filter == filter_) {
        return;
    }
    filter_ = filter;
    if (firstPage_) {
        rebuildItems(false);
    }
    changed();
}

void MangaModel::rebuildItems(bool fieldsChanged) {
    std::vector<core::Chapter> next;
    next.reserve(loaded_.size());
    for (const core::Chapter& chapter : loaded_) {
        const bool shown = filter_ == ChapterFilter::All || (filter_ == ChapterFilter::Unread && !chapter.isRead) ||
                           (filter_ == ChapterFilter::Bookmarked && chapter.isBookmarked);
        if (shown) {
            next.push_back(chapter);
        }
    }
    const bool newest = sort_ == ChapterSort::NewestFirst;
    std::stable_sort(next.begin(), next.end(), [newest](const core::Chapter& a, const core::Chapter& b) {
        return newest ? a.sourceOrder > b.sourceOrder : a.sourceOrder < b.sourceOrder;
    });
    // The rows on screen stay where they are when the old list is the start of the new one (more chapters at the end).
    bool prefix = next.size() >= items_.size();
    for (std::size_t i = 0; prefix && i < items_.size(); ++i) {
        prefix = items_[i].id == next[i].id;
    }
    if (!prefix) {
        ++listRevision_;
    } else if (fieldsChanged) {
        ++itemsRevision_;
    }
    items_ = std::move(next);
}

void MangaModel::updateState() {
    if (state_ == State::Failed) {
        return;
    }
    if (!detailsLoaded_ || !firstPage_) {
        state_ = State::Loading;
    } else {
        state_ = loaded_.empty() ? State::Empty : State::Ready;
    }
}

void MangaModel::updateUnreadCount() {
    // Counted on the console once the whole list is here, so it changes as soon as an action's answer arrives.
    if (complete_ && detailsLoaded_) {
        manga_.unreadCount = static_cast<int>(
            std::count_if(loaded_.begin(), loaded_.end(), [](const core::Chapter& c) { return !c.isRead; }));
    }
}

void MangaModel::fail(const api::ApiError& error) {
    loadCancel_.cancel();  // the other half of the load is no use now
    loadCancel_ = net::CancelToken();
    error_ = error;
    state_ = State::Failed;
    items_.clear();
    ++listRevision_;
    changed();
}

std::vector<MangaModel::Action> MangaModel::actionsFor(std::size_t index) const {
    if (index >= items_.size()) {
        return {};
    }
    const core::Chapter& chapter = items_[index];
    return {chapter.isRead ? Action::MarkUnread : Action::MarkRead,
            chapter.isBookmarked ? Action::RemoveBookmark : Action::Bookmark, Action::MarkPreviousRead,
            Action::MarkPreviousUnread};
}

MangaModel::Action MangaModel::toggleReadAction(std::size_t index) const {
    return index < items_.size() && items_[index].isRead ? Action::MarkUnread : Action::MarkRead;
}

std::vector<int> MangaModel::previousIds(const core::Chapter& target, bool wantRead) const {
    std::vector<int> ids;
    for (const core::Chapter& chapter : loaded_) {
        if (chapter.sourceOrder >= target.sourceOrder) {
            continue;
        }
        const bool started = chapter.isRead || chapter.lastPageRead > 0;
        if (wantRead ? started : !chapter.isRead) {
            ids.push_back(chapter.id);
        }
    }
    return ids;
}

bool MangaModel::act(std::size_t index, Action action) {
    if (busy_ || index >= items_.size()) {
        return false;
    }
    const core::Chapter& target = items_[index];
    std::vector<int> ids;
    core::ChapterPatch patch;
    switch (action) {
        case Action::MarkRead:
            if (!target.isRead) {
                ids.push_back(target.id);
            }
            patch.isRead = true;
            break;
        case Action::MarkUnread:
            if (target.isRead || target.lastPageRead > 0) {
                ids.push_back(target.id);
            }
            patch.isRead = false;
            patch.lastPageRead = 0;  // unread starts from the first page again (D43)
            break;
        case Action::Bookmark:
            if (!target.isBookmarked) {
                ids.push_back(target.id);
            }
            patch.isBookmarked = true;
            break;
        case Action::RemoveBookmark:
            if (target.isBookmarked) {
                ids.push_back(target.id);
            }
            patch.isBookmarked = false;
            break;
        case Action::MarkPreviousRead:
            ids = previousIds(target, false);
            patch.isRead = true;
            break;
        case Action::MarkPreviousUnread:
            ids = previousIds(target, true);
            patch.isRead = false;
            patch.lastPageRead = 0;
            break;
    }
    if (ids.empty()) {
        return false;
    }
    busy_ = true;
    changed();
    const net::CancelToken token = actionCancel_;
    client_.updateChapters(std::move(ids), patch, token)
        .then([this, token](const api::ApiResult<std::vector<core::Chapter>>& result) {
            if (token.cancelled()) {
                return;
            }
            busy_ = false;
            if (result.ok()) {
                applyChanged(result.value());
            } else {
                actionError_ = result.error();
            }
            changed();
        });
    return true;
}

void MangaModel::applyChanged(const std::vector<core::Chapter>& changedChapters) {
    std::map<int, const core::Chapter*> byId;
    for (const core::Chapter& chapter : changedChapters) {
        byId[chapter.id] = &chapter;
    }
    for (core::Chapter& chapter : loaded_) {
        const auto found = byId.find(chapter.id);
        if (found == byId.end()) {
            continue;
        }
        const core::Chapter& saved = *found->second;
        chapter.isRead = saved.isRead;
        chapter.isBookmarked = saved.isBookmarked;
        chapter.lastPageRead = saved.lastPageRead;
        chapter.pageCount = saved.pageCount;
        chapter.lastReadAt = saved.lastReadAt;
    }
    rebuildItems(true);
    updateUnreadCount();
}

std::optional<api::ApiError> MangaModel::takeActionError() {
    std::optional<api::ApiError> error = std::move(actionError_);
    actionError_.reset();
    return error;
}

const core::Chapter* MangaModel::chapter(int chapterId) const {
    const auto found = std::find_if(loaded_.begin(), loaded_.end(),
                                    [chapterId](const core::Chapter& c) { return c.id == chapterId; });
    return found == loaded_.end() ? nullptr : &*found;
}

void MangaModel::rememberFocus(std::size_t index) {
    if (index < items_.size()) {
        focusId_ = items_[index].id;
        focusIndex_ = index;
    }
}

std::size_t MangaModel::rememberedFocus() const {
    if (items_.empty()) {
        return 0;
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == focusId_) {
            return i;
        }
    }
    return std::min(focusIndex_, items_.size() - 1);
}

}  // namespace snx::ui
