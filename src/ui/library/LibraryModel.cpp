#include "ui/library/LibraryModel.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

void sortLibrary(std::vector<core::Manga>& manga, core::LibrarySort sort) {
    // stable_sort: equal manga keep the server's order (title, then id).
    switch (sort) {
        case core::LibrarySort::Title:
            break;
        case core::LibrarySort::Unread:
            std::stable_sort(manga.begin(), manga.end(), [](const core::Manga& a, const core::Manga& b) {
                return a.unreadCount > b.unreadCount;
            });
            break;
        case core::LibrarySort::LastRead:
            std::stable_sort(manga.begin(), manga.end(), [](const core::Manga& a, const core::Manga& b) {
                return a.lastReadAt > b.lastReadAt;  // never read (0) goes last
            });
            break;
    }
}

LibraryModel::LibraryModel(api::SuwayomiClient& client, core::LibrarySettings start, int pageSize)
    : client_(client), settings_(start), nearEnd_(static_cast<std::size_t>(std::max(1, pageSize / 2))) {}

LibraryModel::~LibraryModel() {
    cancel_.cancel();
}

void LibraryModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void LibraryModel::changed() {
    if (listener_) {
        listener_();
    }
}

void LibraryModel::cancelRunning() {
    cancel_.cancel();
    cancel_ = net::CancelToken();
    loading_ = false;
}

void LibraryModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    loadCategories();
}

void LibraryModel::refresh() {
    started_ = true;
    cancelRunning();
    shelves_.clear();
    categories_.clear();
    pending_.clear();
    items_.clear();
    current_ = 0;
    loadCategories();
}

void LibraryModel::loadCategories() {
    state_ = State::Loading;
    loading_ = true;
    changed();
    const net::CancelToken token = cancel_;
    client_.categories(token).then([this, token](const api::ApiResult<std::vector<core::Category>>& result) {
        if (token.cancelled()) {
            return;  // the model may be gone
        }
        onCategories(result);
    });
}

void LibraryModel::onCategories(const api::ApiResult<std::vector<core::Category>>& result) {
    loading_ = false;
    if (!result.ok()) {
        fail(result.error());
        return;
    }
    pending_ = result.value();
    const bool hasDefault = std::any_of(pending_.begin(), pending_.end(),
                                        [](const core::Category& c) { return c.id == core::kDefaultCategoryId; });
    if (!hasDefault) {
        // The server always has it; keep the rule "Default is there when it holds manga" true anyway.
        core::Category fallback;
        fallback.id = core::kDefaultCategoryId;
        fallback.name = "Default";
        fallback.isDefault = true;
        pending_.insert(pending_.begin(), fallback);
    }
    if (pending_.size() == 1) {
        categoriesReady();
        return;
    }
    // Default's first page decides whether it is a tab. If it is the start category, that page is needed anyway.
    loadPage(core::kDefaultCategoryId);
}

void LibraryModel::categoriesReady() {
    categories_ = std::move(pending_);
    pending_.clear();
    const auto start = std::find_if(categories_.begin(), categories_.end(),
                                    [this](const core::Category& c) { return c.id == settings_.categoryId; });
    current_ = start == categories_.end() ? 0 : static_cast<std::size_t>(start - categories_.begin());
    settings_.categoryId = categories_[current_].id;
    showCurrent();
}

void LibraryModel::loadPage(int categoryId) {
    loading_ = true;
    Shelf& target = shelf(categoryId);
    const net::CancelToken token = cancel_;
    client_.library(categoryId, target.nextPage, token)
        .then([this, token, categoryId](const api::ApiResult<core::NodeList<core::Manga>>& result) {
            if (token.cancelled()) {
                return;
            }
            onPage(categoryId, result);
        });
}

void LibraryModel::onPage(int categoryId, const api::ApiResult<core::NodeList<core::Manga>>& result) {
    loading_ = false;
    Shelf& target = shelf(categoryId);
    if (!result.ok()) {
        if (target.started) {
            target.failed = true;  // keep what is loaded
            changed();
        } else {
            shelves_.erase(categoryId);
            fail(result.error());
        }
        return;
    }
    const core::NodeList<core::Manga>& page = result.value();
    for (const core::Manga& manga : page.nodes) {
        if (target.ids.insert(manga.id).second) {
            target.loaded.push_back(manga);
        }
    }
    target.total = page.totalCount;
    target.nextPage += 1;
    target.started = true;
    // An empty page that claims there is more would make the grid ask forever.
    target.complete = !page.hasNextPage || page.nodes.empty();
    target.failed = false;

    if (!pending_.empty()) {
        // Default's first page arrived while the categories wait for it.
        if (target.loaded.empty() && target.complete) {
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                          [](const core::Category& c) { return c.id == core::kDefaultCategoryId; }),
                           pending_.end());
            shelves_.erase(core::kDefaultCategoryId);
        }
        categoriesReady();
        return;
    }
    const core::Category* onScreen = category();
    if (onScreen != nullptr && onScreen->id == categoryId) {
        showCurrent();
    }
}

void LibraryModel::showCurrent() {
    if (categories_.empty()) {
        return;
    }
    const int id = categories_[current_].id;
    Shelf& target = shelf(id);
    const bool needsAll = settings_.sort != core::LibrarySort::Title;
    if (!target.started || (needsAll && !target.complete && !target.failed)) {
        state_ = State::Loading;
        items_.clear();
        changed();
        if (!loading_) {
            loadPage(id);
        }
        return;
    }
    items_ = target.loaded;
    sortLibrary(items_, settings_.sort);
    state_ = items_.empty() ? State::Empty : State::Ready;
    changed();
}

void LibraryModel::fail(const api::ApiError& error) {
    error_ = error;
    state_ = State::Failed;
    items_.clear();
    changed();
}

void LibraryModel::selectCategory(std::size_t index) {
    if (index >= categories_.size() || index == current_) {
        return;
    }
    cancelRunning();  // a page of the category left behind: it loads again when the user comes back
    current_ = index;
    settings_.categoryId = categories_[current_].id;
    showCurrent();
}

void LibraryModel::stepCategory(int step) {
    if (categories_.empty()) {
        return;
    }
    const auto last = static_cast<long>(categories_.size()) - 1;
    const long next = std::clamp(static_cast<long>(current_) + step, 0L, last);
    selectCategory(static_cast<std::size_t>(next));
}

void LibraryModel::setSort(core::LibrarySort sort) {
    if (sort == settings_.sort) {
        return;
    }
    settings_.sort = sort;
    for (auto& [id, kept] : shelves_) {
        kept.focus = 0;  // a new order: start at the top
    }
    if (state_ == State::Failed || categories_.empty()) {
        changed();  // nothing to sort yet; the new sort applies once there is
        return;
    }
    showCurrent();
}

void LibraryModel::showing(std::size_t lastIndex) {
    if (state_ != State::Ready || loading_ || settings_.sort != core::LibrarySort::Title) {
        return;
    }
    Shelf& target = shelf(categories_[current_].id);
    if (target.complete || target.failed) {
        return;
    }
    if (lastIndex + nearEnd_ >= target.loaded.size()) {
        loadPage(categories_[current_].id);
        changed();  // loadingMore() is true now
    }
}

const core::Category* LibraryModel::category() const {
    return categories_.empty() ? nullptr : &categories_[current_];
}

const LibraryModel::Shelf* LibraryModel::currentShelf() const {
    if (categories_.empty()) {
        return nullptr;
    }
    const auto it = shelves_.find(categories_[current_].id);
    return it == shelves_.end() ? nullptr : &it->second;
}

int LibraryModel::totalCount() const {
    const Shelf* s = currentShelf();
    return s ? s->total : 0;
}

std::size_t LibraryModel::loadedCount() const {
    const Shelf* s = currentShelf();
    return s ? s->loaded.size() : 0;
}

bool LibraryModel::moreFailed() const {
    const Shelf* s = currentShelf();
    return s != nullptr && s->failed;
}

bool LibraryModel::libraryEmpty() const {
    return state_ == State::Empty && categories_.size() == 1 && categories_[0].id == core::kDefaultCategoryId;
}

void LibraryModel::rememberFocus(std::size_t index) {
    if (!categories_.empty()) {
        shelf(categories_[current_].id).focus = index;
    }
}

void LibraryModel::updateManga(const core::Manga& manga) {
    bool found = false;
    for (auto& [id, kept] : shelves_) {
        for (core::Manga& entry : kept.loaded) {
            if (entry.id == manga.id) {
                entry.unreadCount = manga.unreadCount;
                found = true;
            }
        }
    }
    if (!found) {
        return;
    }
    for (core::Manga& entry : items_) {
        if (entry.id == manga.id) {
            entry.unreadCount = manga.unreadCount;
        }
    }
    sortLibrary(items_, settings_.sort);  // stable: with the title sort nothing moves
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == manga.id) {
            rememberFocus(i);
        }
    }
    ++revision_;
    changed();
}

std::size_t LibraryModel::rememberedFocus() const {
    const Shelf* s = currentShelf();
    if (s == nullptr || items_.empty()) {
        return 0;
    }
    return std::min(s->focus, items_.size() - 1);
}

}  // namespace snx::ui
