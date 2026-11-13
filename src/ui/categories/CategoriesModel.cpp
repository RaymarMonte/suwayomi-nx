#include "ui/categories/CategoriesModel.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace snx::ui {

CategoriesModel::CategoriesModel(api::SuwayomiClient& client, std::size_t nameMaxChars)
    : client_(client), nameMaxChars_(nameMaxChars) {}

CategoriesModel::~CategoriesModel() {
    cancel_.cancel();
}

void CategoriesModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void CategoriesModel::changed() {
    if (listener_) {
        listener_();
    }
}

void CategoriesModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    load();
}

void CategoriesModel::retry() {
    if (state_ != State::Failed) {
        return;
    }
    load();
}

void CategoriesModel::load() {
    state_ = State::Loading;
    changed();
    const net::CancelToken token = cancel_;
    client_.manageCategories(token).then([this, token](const api::ApiResult<std::vector<core::Category>>& result) {
        if (token.cancelled()) {
            return;  // the model may be gone
        }
        if (!result.ok()) {
            error_ = result.error();
            state_ = State::Failed;
            changed();
            return;
        }
        setAll(result.value());
        changed();
    });
}

void CategoriesModel::setAll(std::vector<core::Category> all) {
    std::map<int, int> counts;
    for (const core::Category& c : all_) {
        counts[c.id] = c.mangaCount;
    }
    for (core::Category& c : all) {
        if (c.mangaCount < 0) {
            const auto known = counts.find(c.id);
            c.mangaCount = known == counts.end() ? 0 : known->second;  // a new category holds none
        }
    }
    std::stable_sort(all.begin(), all.end(),
                     [](const core::Category& a, const core::Category& b) { return a.order < b.order; });
    all_ = std::move(all);
    items_.clear();
    for (const core::Category& c : all_) {
        if (c.id != core::kDefaultCategoryId) {
            items_.push_back(c);
        }
    }
    state_ = items_.empty() ? State::Empty : State::Ready;
    ++revision_;
}

core::NameProblem CategoriesModel::checkName(const std::string& typed, long index) const {
    const int exceptId =
        index >= 0 && static_cast<std::size_t>(index) < items_.size() ? items_[static_cast<std::size_t>(index)].id : -1;
    return core::checkCategoryName(core::trimText(typed), all_, exceptId, nameMaxChars_);
}

bool CategoriesModel::add(const std::string& typed) {
    if (busy_ || (state_ != State::Ready && state_ != State::Empty) || checkName(typed) != core::NameProblem::None) {
        return false;
    }
    busy_ = true;
    changed();
    const net::CancelToken token = cancel_;
    client_.createCategory(core::trimText(typed), token).then([this, token](const api::ApiResult<core::Category>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            finish(Action::Add, 0, &result.error());
            return;
        }
        std::vector<core::Category> all = all_;
        core::Category created = result.value();
        created.mangaCount = 0;
        all.push_back(created);
        setAll(std::move(all));
        focusId_ = created.id;
        changedAnything_ = true;
        finish(Action::Add, created.id, nullptr);
    });
    return true;
}

bool CategoriesModel::renameAt(std::size_t index, const std::string& typed) {
    if (busy_ || state_ != State::Ready || index >= items_.size()) {
        return false;
    }
    const std::string name = core::trimText(typed);
    if (name == items_[index].name || checkName(typed, static_cast<long>(index)) != core::NameProblem::None) {
        return false;
    }
    busy_ = true;
    changed();
    const int id = items_[index].id;
    const net::CancelToken token = cancel_;
    client_.renameCategory(id, name, token).then([this, token, id](const api::ApiResult<core::Category>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            finish(Action::Rename, id, &result.error());
            return;
        }
        std::vector<core::Category> all = all_;
        for (core::Category& c : all) {
            if (c.id == id) {
                c.name = result.value().name;
            }
        }
        setAll(std::move(all));
        focusId_ = id;
        changedAnything_ = true;
        finish(Action::Rename, id, nullptr);
    });
    return true;
}

bool CategoriesModel::move(std::size_t index, int step) {
    if (busy_ || state_ != State::Ready || index >= items_.size()) {
        return false;
    }
    const int id = items_[index].id;
    const int position = core::categoryPosition(items_, id, step);
    if (position == 0) {
        return false;
    }
    busy_ = true;
    changed();
    const net::CancelToken token = cancel_;
    client_.moveCategory(id, position, token)
        .then([this, token, id](const api::ApiResult<std::vector<core::Category>>& result) {
            if (token.cancelled()) {
                return;
            }
            if (!result.ok()) {
                finish(Action::Move, id, &result.error());
                return;
            }
            setAll(result.value());
            focusId_ = id;  // the focus moves with it
            changedAnything_ = true;
            finish(Action::Move, id, nullptr);
        });
    return true;
}

bool CategoriesModel::deleteAt(std::size_t index) {
    if (busy_ || state_ != State::Ready || index >= items_.size()) {
        return false;
    }
    busy_ = true;
    changed();
    const int id = items_[index].id;
    const net::CancelToken token = cancel_;
    client_.deleteCategory(id, token).then([this, token, id, index](const api::ApiResult<int>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            finish(Action::Delete, id, &result.error());
            return;
        }
        std::vector<core::Category> all;
        for (const core::Category& c : all_) {
            if (c.id != id) {
                all.push_back(c);
            }
        }
        setAll(std::move(all));
        // The focus stays at the same place: the next row (or the last one).
        focusId_ = items_.empty() ? 0 : items_[std::min(index, items_.size() - 1)].id;
        changedAnything_ = true;
        finish(Action::Delete, id, nullptr);
    });
    return true;
}

void CategoriesModel::finish(Action action, int categoryId, const api::ApiError* error) {
    busy_ = false;
    Done done;
    done.action = action;
    done.ok = error == nullptr;
    done.categoryId = categoryId;
    done_ = done;
    if (error != nullptr) {
        actionError_ = *error;
    }
    changed();
}

std::optional<CategoriesModel::Done> CategoriesModel::takeDone() {
    std::optional<Done> done = done_;
    done_.reset();
    return done;
}

std::optional<api::ApiError> CategoriesModel::takeActionError() {
    std::optional<api::ApiError> error = actionError_;
    actionError_.reset();
    return error;
}

void CategoriesModel::rememberFocus(std::size_t index) {
    if (index < items_.size()) {
        focusId_ = items_[index].id;
        focusIndex_ = index;
    }
}

std::size_t CategoriesModel::rememberedFocus() const {
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
