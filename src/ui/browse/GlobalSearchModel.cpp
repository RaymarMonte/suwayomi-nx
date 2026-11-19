#include "ui/browse/GlobalSearchModel.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "core/SourceLanguages.hpp"

namespace snx::ui {

GlobalSearchModel::GlobalSearchModel(api::SuwayomiClient& client, const std::vector<core::Source>& sources,
                                     const std::vector<std::string>& hiddenLanguages, Limits limits)
    : client_(client), limits_(limits) {
    limits_.concurrency = std::max<std::size_t>(1, limits_.concurrency);
    for (std::size_t index : core::searchSources(sources, hiddenLanguages, limits_.sourcesMax)) {
        Row row;
        row.source = sources[index];
        rows_.push_back(std::move(row));
    }
}

GlobalSearchModel::~GlobalSearchModel() {
    cancel_.cancel();
}

void GlobalSearchModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void GlobalSearchModel::changed() {
    if (listener_) {
        listener_();
    }
}

std::size_t GlobalSearchModel::doneCount() const {
    return static_cast<std::size_t>(std::count_if(rows_.begin(), rows_.end(), [](const Row& row) {
        return row.state == RowState::Found || row.state == RowState::NothingFound || row.state == RowState::Failed;
    }));
}

std::size_t GlobalSearchModel::runningCount() const {
    return static_cast<std::size_t>(
        std::count_if(rows_.begin(), rows_.end(), [](const Row& row) { return row.state == RowState::Searching; }));
}

bool GlobalSearchModel::search(std::string query) {
    if (query.empty()) {
        return false;
    }
    cancel_.cancel();  // what ran for the last text: its answers are dropped
    cancel_ = net::CancelToken();
    query_ = std::move(query);
    for (Row& row : rows_) {
        const core::Source source = row.source;
        row = Row();
        row.source = source;
    }
    ++listRevision_;
    startNext();
    return true;
}

void GlobalSearchModel::startNext() {
    // First mark the rows that start (the screen shows "Searching..." at once), then ask: an answer can come back inside
    // ask() (the tests' fake server), and it starts the next one itself.
    std::vector<std::size_t> starting;
    std::size_t running = runningCount();
    for (std::size_t i = 0; i < rows_.size() && running < limits_.concurrency; ++i) {
        if (rows_[i].state == RowState::Waiting) {
            rows_[i].state = RowState::Searching;
            starting.push_back(i);
            ++running;
        }
    }
    changed();
    const net::CancelToken token = cancel_;
    for (std::size_t i : starting) {
        if (token.cancelled()) {
            return;  // the model is gone or searches something else now
        }
        ask(i);
    }
}

void GlobalSearchModel::ask(std::size_t row) {
    const net::CancelToken token = cancel_;
    client_
        .browseSource(rows_[row].source.id, core::gql::BrowseKind::Search, 1, query_, nlohmann::json::array(), token)
        .then([this, token, row](const api::ApiResult<core::BrowsePage>& result) {
            if (token.cancelled()) {
                return;  // the model may be gone
            }
            answered(row, result);
        });
}

void GlobalSearchModel::answered(std::size_t row, const api::ApiResult<core::BrowsePage>& result) {
    if (row >= rows_.size()) {
        return;
    }
    Row& r = rows_[row];
    if (!result.ok()) {
        r.state = RowState::Failed;
        r.error = result.error();
    } else {
        std::set<int> seen;
        for (const core::Manga& manga : result.value().manga) {
            if (!seen.insert(manga.id).second) {
                continue;  // a manga twice on the page shows once
            }
            if (r.manga.size() < limits_.covers) {
                core::Manga shown = manga;
                if (!shown.source) {
                    shown.source = r.source;  // "Loading from <source>..." on the preview
                }
                r.manga.push_back(std::move(shown));
            }
        }
        r.found = seen.size();
        r.more = result.value().hasNextPage;
        r.state = r.found > 0 ? RowState::Found : RowState::NothingFound;
    }
    ++itemsRevision_;
    startNext();  // the free place goes to the next waiting source (and tells the screen)
}

void GlobalSearchModel::setInLibrary(int mangaId, bool inLibrary) {
    bool any = false;
    for (Row& row : rows_) {
        for (core::Manga& manga : row.manga) {
            if (manga.id == mangaId && manga.inLibrary != inLibrary) {
                manga.inLibrary = inLibrary;
                any = true;
            }
        }
    }
    if (any) {
        ++itemsRevision_;
        changed();
    }
}

std::string_view globalSearchStateName(GlobalSearchModel::RowState state) {
    switch (state) {
        case GlobalSearchModel::RowState::Waiting:
            return "waiting";
        case GlobalSearchModel::RowState::Searching:
            return "searching";
        case GlobalSearchModel::RowState::Found:
            return "found";
        case GlobalSearchModel::RowState::NothingFound:
            return "nothing_found";
        case GlobalSearchModel::RowState::Failed:
            return "failed";
    }
    return "waiting";
}

}  // namespace snx::ui
