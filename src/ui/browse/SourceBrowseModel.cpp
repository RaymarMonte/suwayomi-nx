#include "ui/browse/SourceBrowseModel.hpp"

#include <algorithm>
#include <utility>

#include "core/graphql/Operations.hpp"

namespace snx::ui {

std::string_view browseModeName(SourceBrowseModel::Mode mode) {
    switch (mode) {
        case SourceBrowseModel::Mode::Latest:
            return "latest";
        case SourceBrowseModel::Mode::Search:
            return "search";
        case SourceBrowseModel::Mode::Popular:
            break;
    }
    return "popular";
}

SourceBrowseModel::SourceBrowseModel(api::SuwayomiClient& client, core::Source source, std::size_t maxItems)
    : client_(client), source_(std::move(source)), maxItems_(std::max<std::size_t>(1, maxItems)) {}

SourceBrowseModel::~SourceBrowseModel() {
    cancel_.cancel();
}

void SourceBrowseModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void SourceBrowseModel::changed() {
    if (listener_) {
        listener_();
    }
}

void SourceBrowseModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    restart();
}

void SourceBrowseModel::refresh() {
    started_ = true;
    restart();
}

void SourceBrowseModel::retry() {
    if (state_ == State::Failed) {
        restart();
    } else if (moreFailed_ && !loading_) {
        moreFailed_ = false;
        loadPage();
        changed();
    }
}

void SourceBrowseModel::setMode(Mode mode) {
    if (mode == Mode::Search || (mode == Mode::Latest && !source_.supportsLatest)) {
        return;
    }
    if (mode == mode_ && started_) {
        return;
    }
    mode_ = mode;
    query_.clear();
    changes_ = nlohmann::json::array();
    filtersChanged_ = 0;
    started_ = true;
    restart();
}

void SourceBrowseModel::search(std::string query, nlohmann::json filterChanges, int filtersChanged) {
    if (!filterChanges.is_array()) {
        filterChanges = nlohmann::json::array();
    }
    if (query.empty() && filterChanges.empty()) {
        clearSearch();
        return;
    }
    mode_ = Mode::Search;
    query_ = std::move(query);
    changes_ = std::move(filterChanges);
    filtersChanged_ = std::max(0, filtersChanged);
    started_ = true;
    restart();
}

void SourceBrowseModel::clearSearch() {
    const bool wasSearch = mode_ == Mode::Search;
    mode_ = Mode::Popular;
    query_.clear();
    changes_ = nlohmann::json::array();
    filtersChanged_ = 0;
    if (wasSearch || !started_) {
        started_ = true;
        restart();
    }
}

void SourceBrowseModel::restart() {
    cancel_.cancel();
    cancel_ = net::CancelToken();
    items_.clear();
    ids_.clear();
    nextPage_ = 1;
    nearEnd_ = 1;
    loading_ = false;
    complete_ = false;
    moreFailed_ = false;
    focus_ = 0;
    state_ = State::Loading;
    ++listRevision_;
    loadPage();
    changed();
}

void SourceBrowseModel::loadPage() {
    loading_ = true;
    const net::CancelToken token = cancel_;
    const core::gql::BrowseKind kind = mode_ == Mode::Search   ? core::gql::BrowseKind::Search
                                       : mode_ == Mode::Latest ? core::gql::BrowseKind::Latest
                                                               : core::gql::BrowseKind::Popular;
    client_.browseSource(source_.id, kind, nextPage_, query_, changes_, token)
        .then([this, token](const api::ApiResult<core::BrowsePage>& result) {
            if (token.cancelled()) {
                return;  // the model may be gone, or the list was replaced meanwhile
            }
            onPage(result);
        });
}

void SourceBrowseModel::onPage(const api::ApiResult<core::BrowsePage>& result) {
    loading_ = false;
    const bool first = nextPage_ == 1;
    if (!result.ok()) {
        if (first) {
            error_ = result.error();
            state_ = State::Failed;
            ++listRevision_;
        } else {
            error_ = result.error();
            moreFailed_ = true;
        }
        changed();
        return;
    }
    const core::BrowsePage& page = result.value();
    if (first) {
        nearEnd_ = std::max<std::size_t>(1, page.manga.size() / 2);
    }
    std::size_t added = 0;
    for (const core::Manga& manga : page.manga) {
        if (items_.size() >= maxItems_) {
            break;
        }
        if (ids_.insert(manga.id).second) {
            items_.push_back(manga);
            ++added;
        }
    }
    ++nextPage_;
    // The end: the source says so, a page brought nothing new (Local ignores the page), or the limit (S6).
    complete_ = !page.hasNextPage || added == 0 || items_.size() >= maxItems_;
    state_ = items_.empty() ? State::Empty : State::Ready;
    changed();
}

void SourceBrowseModel::showing(std::size_t lastIndex) {
    if (state_ != State::Ready || loading_ || complete_ || moreFailed_) {
        return;
    }
    if (lastIndex + nearEnd_ >= items_.size()) {
        loadPage();
        changed();
    }
}

void SourceBrowseModel::setInLibrary(int mangaId, bool inLibrary) {
    for (core::Manga& manga : items_) {
        if (manga.id == mangaId && manga.inLibrary != inLibrary) {
            manga.inLibrary = inLibrary;
            ++itemsRevision_;
            changed();
            return;
        }
    }
}

}  // namespace snx::ui
