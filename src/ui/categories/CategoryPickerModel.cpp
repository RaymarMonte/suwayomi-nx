#include "ui/categories/CategoryPickerModel.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

namespace {

std::set<int> idsOf(const std::vector<core::Category>& categories) {
    std::set<int> ids;
    for (const core::Category& c : categories) {
        if (c.id != core::kDefaultCategoryId) {
            ids.insert(c.id);
        }
    }
    return ids;
}

}  // namespace

CategoryPickerModel::CategoryPickerModel(api::SuwayomiClient& client, int mangaId,
                                         std::optional<std::vector<core::Category>> current, std::size_t nameMaxChars)
    : client_(client), mangaId_(mangaId), given_(std::move(current)), nameMaxChars_(nameMaxChars) {}

CategoryPickerModel::~CategoryPickerModel() {
    cancel_.cancel();
}

void CategoryPickerModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void CategoryPickerModel::changedNow() {
    if (listener_) {
        listener_();
    }
}

void CategoryPickerModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    load();
}

void CategoryPickerModel::retry() {
    if (state_ != State::Failed) {
        return;
    }
    load();
}

void CategoryPickerModel::load() {
    state_ = State::Loading;
    failed_ = false;
    categories_.reset();
    initial_.reset();
    waiting_ = 0;
    changedNow();
    const net::CancelToken token = cancel_;
    if (given_) {
        initial_ = idsOf(*given_);
    } else {
        ++waiting_;
    }
    ++waiting_;
    client_.categories(token).then([this, token](const api::ApiResult<std::vector<core::Category>>& result) {
        if (token.cancelled()) {
            return;
        }
        --waiting_;
        if (result.ok()) {
            categories_ = result.value();
        } else if (!failed_) {
            failed_ = true;
            error_ = result.error();
        }
        loaded();
    });
    if (!given_) {
        client_.manga(mangaId_, token).then([this, token](const api::ApiResult<core::Manga>& result) {
            if (token.cancelled()) {
                return;
            }
            --waiting_;
            if (result.ok()) {
                initial_ = idsOf(result.value().categories);
            } else if (!failed_) {
                failed_ = true;
                error_ = result.error();
            }
            loaded();
        });
    }
}

void CategoryPickerModel::loaded() {
    if (waiting_ > 0) {
        return;
    }
    if (failed_ || !categories_ || !initial_) {
        state_ = State::Failed;
        changedNow();
        return;
    }
    rows_.clear();
    std::vector<core::Category> sorted = *categories_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const core::Category& a, const core::Category& b) { return a.order < b.order; });
    for (const core::Category& c : sorted) {
        if (c.id != core::kDefaultCategoryId) {
            rows_.push_back({c, initial_->count(c.id) > 0});
        }
    }
    state_ = State::Ready;
    ++revision_;
    changedNow();
}

void CategoryPickerModel::toggle(std::size_t index) {
    if (state_ != State::Ready || adding_ || index >= rows_.size()) {
        return;
    }
    rows_[index].checked = !rows_[index].checked;
    ++revision_;
    changedNow();
}

std::set<int> CategoryPickerModel::chosen() const {
    std::set<int> ids;
    for (const Row& row : rows_) {
        if (row.checked) {
            ids.insert(row.category.id);
        }
    }
    return ids;
}

bool CategoryPickerModel::changed() const {
    return initial_ && chosen() != *initial_;
}

core::NameProblem CategoryPickerModel::checkName(const std::string& typed) const {
    std::vector<core::Category> all = categories_ ? *categories_ : std::vector<core::Category>();
    return core::checkCategoryName(core::trimText(typed), all, -1, nameMaxChars_);
}

bool CategoryPickerModel::addCategory(const std::string& typed) {
    if (state_ != State::Ready || adding_ || checkName(typed) != core::NameProblem::None) {
        return false;
    }
    adding_ = true;
    changedNow();
    const net::CancelToken token = cancel_;
    client_.createCategory(core::trimText(typed), token).then([this, token](const api::ApiResult<core::Category>& result) {
        if (token.cancelled()) {
            return;
        }
        adding_ = false;
        if (!result.ok()) {
            actionError_ = result.error();
            changedNow();
            return;
        }
        if (categories_) {
            categories_->push_back(result.value());
        }
        rows_.push_back({result.value(), true});
        ++revision_;
        changedNow();
    });
    return true;
}

bool CategoryPickerModel::save() {
    if (state_ != State::Ready || adding_ || !initial_) {
        return false;
    }
    const core::CategoryChange change = core::categoryChange(*initial_, chosen());
    if (change.empty()) {
        saved_.clear();
        for (const Row& row : rows_) {
            if (row.checked) {
                saved_.push_back(row.category);
            }
        }
        savedChange_ = false;
        state_ = State::Saved;
        changedNow();
        return true;
    }
    state_ = State::Saving;
    changedNow();
    const net::CancelToken token = cancel_;
    client_.setMangaCategories(mangaId_, change.add, change.remove, token)
        .then([this, token](const api::ApiResult<std::vector<core::Category>>& result) {
            if (token.cancelled()) {
                return;
            }
            if (!result.ok()) {
                actionError_ = result.error();
                state_ = State::Ready;  // the ticks stay: Save can be tried again
                changedNow();
                return;
            }
            saved_ = result.value();
            savedChange_ = true;
            state_ = State::Saved;
            changedNow();
        });
    return true;
}

std::optional<api::ApiError> CategoryPickerModel::takeActionError() {
    std::optional<api::ApiError> error = actionError_;
    actionError_.reset();
    return error;
}

}  // namespace snx::ui
