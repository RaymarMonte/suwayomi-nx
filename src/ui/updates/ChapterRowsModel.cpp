#include "ui/updates/ChapterRowsModel.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

ChapterRowsModel::ChapterRowsModel(api::SuwayomiClient& client, Kind kind, Limits limits)
    : client_(client), kind_(kind), limits_(limits) {
    limits_.screenRows = std::max<std::size_t>(limits_.screenRows, 1);
    limits_.fillPages = std::max(limits_.fillPages, 1);
}

ChapterRowsModel::~ChapterRowsModel() {
    cancel_.cancel();
    actionCancel_.cancel();
}

void ChapterRowsModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void ChapterRowsModel::setMangaChanged(std::function<void(const core::Manga&)> changed) {
    mangaChanged_ = std::move(changed);
}

void ChapterRowsModel::changed() {
    if (listener_) {
        listener_();
    }
}

void ChapterRowsModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    reload();
}

void ChapterRowsModel::reload() {
    started_ = true;
    if (acting_) {
        reloadAfterAction_ = true;
        return;
    }
    reloadAfterAction_ = false;
    cancel_.cancel();
    cancel_ = net::CancelToken();
    loading_ = false;
    fresh_ = true;
    runPages_ = 0;
    wantRows_ = limits_.screenRows;
    if (items_.empty()) {
        state_ = State::Loading;
    }
    loadPage();
    changed();
}

void ChapterRowsModel::loadPage() {
    loading_ = true;
    const int page = fresh_ ? 0 : nextPage_;
    const bool fresh = fresh_;
    const net::CancelToken token = cancel_;
    auto done = [this, token, page, fresh](const api::ApiResult<core::NodeList<core::ChapterEntry>>& result) {
        if (token.cancelled()) {
            return;
        }
        onPage(page, fresh, result);
    };
    if (kind_ == Kind::Updates) {
        client_.updates(page, token).then(done);
    } else {
        client_.history(page, token).then(done);
    }
}

void ChapterRowsModel::onPage(int page, bool fresh, const api::ApiResult<core::NodeList<core::ChapterEntry>>& result) {
    loading_ = false;
    if (!result.ok()) {
        if (fresh && !items_.empty()) {
            pendingError_ = result.error();  // a reload failed: the old rows stay
            fresh_ = false;
        } else if (fresh) {
            error_ = result.error();
            state_ = State::Failed;
        } else {
            moreFailed_ = true;
        }
        changed();
        return;
    }
    if (fresh) {
        fresh_ = false;
        items_.clear();
        filter_ = core::UpdatesFilter();
        seenManga_.clear();
        pages_ = 0;
        hidden_ = 0;
        newest_ = 0;
        ended_ = false;
        moreFailed_ = false;
        listRevision_ += 1;
    }
    const core::NodeList<core::ChapterEntry>& list = result.value();
    std::vector<core::ChapterEntry> rows;
    if (kind_ == Kind::Updates) {
        if (pages_ == 0 && !list.nodes.empty()) {
            newest_ = list.nodes.front().chapter.fetchedAt;
        }
        core::UpdatesPage filtered = core::filterUpdates(list.nodes, filter_);
        hidden_ += filtered.hidden;
        rows = std::move(filtered.rows);
        ended_ = ended_ || filtered.windowEnded;
    } else {
        rows = core::historyRows(list.nodes, seenManga_);
        hidden_ += static_cast<int>(list.nodes.size() - rows.size());
    }
    for (core::ChapterEntry& row : rows) {
        if (items_.size() >= limits_.maxRows) {
            ended_ = true;  // S6: the list never grows past the cap
            break;
        }
        items_.push_back(std::move(row));
    }
    if (items_.size() >= limits_.maxRows) {
        ended_ = true;
    }
    if (!list.hasNextPage || list.nodes.empty()) {
        ended_ = true;
    }
    pages_ += 1;
    runPages_ += 1;
    nextPage_ = page + 1;
    if (!items_.empty()) {
        state_ = State::Ready;
    } else if (ended_) {
        state_ = State::Empty;
    } else {
        state_ = State::Loading;  // every chapter so far hidden: the next page comes by itself
    }
    maybeLoadMore();
    changed();
}

void ChapterRowsModel::maybeLoadMore() {
    if (loading_ || ended_ || moreFailed_) {
        return;
    }
    // A run fills a screen of rows, at most fillPages pages; an empty list keeps going (nothing to scroll to yet).
    if (items_.size() < wantRows_ && (runPages_ < limits_.fillPages || items_.empty())) {
        loadPage();
    }
}

void ChapterRowsModel::showing(std::size_t lastIndex) {
    if (loading_ || ended_ || moreFailed_ || state_ != State::Ready) {
        return;
    }
    if (lastIndex + limits_.screenRows < items_.size()) {
        return;  // not near the end yet
    }
    runPages_ = 0;
    wantRows_ = items_.size() + limits_.screenRows;
    loadPage();
    changed();
}

bool ChapterRowsModel::toggleRead(std::size_t index) {
    if (index >= items_.size()) {
        return false;
    }
    const core::Chapter& chapter = items_[index].chapter;
    return setRead(index, !chapter.isRead);  // as the details screen's X (D43): unread, also with progress, becomes read
}

bool ChapterRowsModel::setRead(std::size_t index, bool read) {
    if (acting_ || index >= items_.size()) {
        return false;
    }
    const core::Chapter& chapter = items_[index].chapter;
    core::ChapterPatch patch;
    if (read) {
        if (chapter.isRead) {
            return false;
        }
        patch.isRead = true;
    } else {
        if (!chapter.isRead && chapter.lastPageRead <= 0) {
            return false;
        }
        patch.isRead = false;
        patch.lastPageRead = 0;  // unread starts from the first page again (D43)
    }
    acting_ = true;
    actionCancel_ = net::CancelToken();
    const net::CancelToken token = actionCancel_;
    const int chapterId = chapter.id;
    const int mangaId = items_[index].manga.id;
    client_.updateChapters({chapterId}, patch, token)
        .then([this, token, chapterId, mangaId](const api::ApiResult<std::vector<core::Chapter>>& result) {
            if (token.cancelled()) {
                return;
            }
            acting_ = false;
            if (!result.ok()) {
                pendingError_ = result.error();
                if (reloadAfterAction_) {
                    reload();
                }
                changed();
                return;
            }
            for (const core::Chapter& saved : result.value()) {
                for (core::ChapterEntry& row : items_) {
                    if (row.chapter.id == saved.id) {
                        row.chapter.isRead = saved.isRead;
                        row.chapter.isBookmarked = saved.isBookmarked;
                        row.chapter.lastPageRead = saved.lastPageRead;
                        row.chapter.lastReadAt = saved.lastReadAt;
                        if (saved.pageCount >= 0) {
                            row.chapter.pageCount = saved.pageCount;
                        }
                    }
                }
            }
            itemsRevision_ += 1;
            if (reloadAfterAction_) {
                reload();  // a reload came meanwhile: now it reads what the server saved
            }
            changed();
            // The library's unread badge follows: the manga's new unread count.
            if (mangaChanged_) {
                client_.manga(mangaId, token).then([this, token](const api::ApiResult<core::Manga>& manga) {
                    if (!token.cancelled() && manga.ok() && mangaChanged_) {
                        mangaChanged_(manga.value());
                    }
                });
            }
        });
    changed();
    return true;
}

std::optional<api::ApiError> ChapterRowsModel::takeError() {
    std::optional<api::ApiError> error = std::move(pendingError_);
    pendingError_.reset();
    return error;
}

}  // namespace snx::ui
