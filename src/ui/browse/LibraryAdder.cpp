#include "ui/browse/LibraryAdder.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

std::string_view adderStepName(LibraryAdder::Step step) {
    switch (step) {
        case LibraryAdder::Step::Idle:
            return "idle";
        case LibraryAdder::Step::Checking:
            return "checking";
        case LibraryAdder::Step::AskDuplicate:
            return "ask_duplicate";
        case LibraryAdder::Step::AskCategories:
            return "ask_categories";
        case LibraryAdder::Step::Adding:
            return "adding";
        case LibraryAdder::Step::Removing:
            return "removing";
        case LibraryAdder::Step::Done:
            return "done";
        case LibraryAdder::Step::Failed:
            return "failed";
    }
    return "idle";
}

LibraryAdder::LibraryAdder(api::SuwayomiClient& client) : client_(client) {}

LibraryAdder::~LibraryAdder() {
    cancel_.cancel();
}

void LibraryAdder::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void LibraryAdder::changed() {
    if (listener_) {
        listener_();
    }
}

std::set<int> LibraryAdder::preticked() const {
    std::set<int> ids;
    for (const core::Category& c : categories_) {
        if (c.isDefault && c.id != core::kDefaultCategoryId) {
            ids.insert(c.id);
        }
    }
    return ids;
}

bool LibraryAdder::add(core::Manga manga, bool askCategories) {
    if (busy()) {
        return false;
    }
    cancel_ = net::CancelToken();
    manga_ = std::move(manga);
    askCategories_ = askCategories;
    removing_ = false;
    duplicate_.reset();
    categories_.clear();
    firstError_.reset();
    asked_ = false;
    sawDuplicate_ = false;
    step_ = Step::Checking;
    waiting_ = 2;
    const net::CancelToken token = cancel_;
    client_.libraryDuplicates(manga_.title, manga_.id, token)
        .then([this, token](const api::ApiResult<std::vector<core::Manga>>& result) {
            if (token.cancelled()) {
                return;
            }
            if (!result.ok()) {
                if (!firstError_) firstError_ = result.error();
            } else if (!result.value().empty()) {
                duplicate_ = result.value().front();
            }
            if (--waiting_ == 0) checked();
        });
    client_.categories(token).then([this, token](const api::ApiResult<std::vector<core::Category>>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            if (!firstError_) firstError_ = result.error();
        } else {
            categories_ = result.value();
        }
        if (--waiting_ == 0) checked();
    });
    changed();
    return true;
}

void LibraryAdder::checked() {
    if (firstError_) {
        fail(*firstError_);
        return;
    }
    if (duplicate_) {
        sawDuplicate_ = true;
        step_ = Step::AskDuplicate;
        changed();
        return;
    }
    decideCategories();
}

void LibraryAdder::addAnyway() {
    if (step_ != Step::AskDuplicate) {
        return;
    }
    decideCategories();
}

void LibraryAdder::decideCategories() {
    const bool hasOwn = std::any_of(categories_.begin(), categories_.end(),
                                    [](const core::Category& c) { return c.id != core::kDefaultCategoryId; });
    if (askCategories_ && hasOwn) {
        asked_ = true;
        step_ = Step::AskCategories;
        changed();
        return;
    }
    const std::set<int> ticked = preticked();
    addWith(std::vector<int>(ticked.begin(), ticked.end()));
}

void LibraryAdder::chooseCategories(std::set<int> ids) {
    if (step_ != Step::AskCategories) {
        return;
    }
    ids.erase(core::kDefaultCategoryId);
    addWith(std::vector<int>(ids.begin(), ids.end()));
}

void LibraryAdder::addWith(std::vector<int> ids) {
    step_ = Step::Adding;
    changed();
    if (manga_.initialized) {
        send(std::move(ids));
        return;
    }
    // Added from a grid without opening it: the details and chapters first, so the library cell has them.
    const net::CancelToken token = cancel_;
    client_.refreshManga(manga_.id, token).then([this, token, ids](const api::ApiResult<core::Manga>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            fail(result.error());
            return;
        }
        const std::string title = manga_.title;
        manga_ = result.value();
        if (manga_.title.empty()) {
            manga_.title = title;
        }
        send(ids);
    });
}

void LibraryAdder::send(std::vector<int> ids) {
    const net::CancelToken token = cancel_;
    client_.addToLibrary(manga_.id, std::move(ids), token).then([this, token](const api::ApiResult<core::Manga>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            fail(result.error());
            return;
        }
        core::Manga now = manga_;
        now.inLibrary = result.value().inLibrary;
        now.inLibraryAt = result.value().inLibraryAt;
        now.categories = result.value().categories;
        now.initialized = true;
        finish(std::move(now));
    });
}

bool LibraryAdder::removeFromLibrary(core::Manga manga) {
    if (busy()) {
        return false;
    }
    cancel_ = net::CancelToken();
    manga_ = std::move(manga);
    removing_ = true;
    duplicate_.reset();
    asked_ = false;
    sawDuplicate_ = false;
    step_ = Step::Removing;
    const net::CancelToken token = cancel_;
    client_.removeFromLibrary(manga_.id, token).then([this, token](const api::ApiResult<core::Manga>& result) {
        if (token.cancelled()) {
            return;
        }
        if (!result.ok()) {
            fail(result.error());
            return;
        }
        core::Manga now = manga_;
        now.inLibrary = result.value().inLibrary;
        finish(std::move(now));
    });
    changed();
    return true;
}

void LibraryAdder::cancel() {
    if (!busy()) {
        return;
    }
    cancel_.cancel();
    cancel_ = net::CancelToken();
    step_ = Step::Idle;
    changed();
}

void LibraryAdder::fail(const api::ApiError& error) {
    error_ = error;
    step_ = Step::Failed;
    changed();
}

void LibraryAdder::finish(core::Manga result) {
    result_ = std::move(result);
    step_ = Step::Done;
    changed();
}

}  // namespace snx::ui
