#include "ui/browse/SourcesModel.hpp"

#include <algorithm>
#include <utility>

namespace snx::ui {

SourcesModel::SourcesModel(api::SuwayomiClient& client, std::string lastSourceId, std::vector<std::string> hiddenLanguages)
    : client_(client), lastSourceId_(std::move(lastSourceId)), hidden_(std::move(hiddenLanguages)) {}

SourcesModel::~SourcesModel() {
    cancel_.cancel();
}

void SourcesModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void SourcesModel::changed() {
    if (listener_) {
        listener_();
    }
}

void SourcesModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    load();
}

void SourcesModel::refresh() {
    started_ = true;
    cancel_.cancel();
    cancel_ = net::CancelToken();
    load();
}

void SourcesModel::load() {
    state_ = State::Loading;
    changed();
    const net::CancelToken token = cancel_;
    client_.sources(token).then([this, token](const api::ApiResult<std::vector<core::Source>>& result) {
        if (token.cancelled()) {
            return;  // the model may be gone
        }
        if (!result.ok()) {
            error_ = result.error();
            state_ = State::Failed;
            sources_.clear();
            rows_.clear();
            ++revision_;
            changed();
            return;
        }
        sources_ = result.value();
        rebuild();
        changed();
    });
}

void SourcesModel::rebuild() {
    rows_ = core::sourceRows(sources_, lastSourceId_, hidden_);
    const bool onlyLocal = std::all_of(sources_.begin(), sources_.end(),
                                       [](const core::Source& s) { return core::isLocalSource(s); });
    state_ = onlyLocal ? State::OnlyLocal : State::Ready;
    ++revision_;
}

const core::Source* SourcesModel::sourceAt(std::size_t row) const {
    if (row >= rows_.size() || rows_[row].source >= sources_.size()) {
        return nullptr;
    }
    return &sources_[rows_[row].source];
}

void SourcesModel::opened(const std::string& sourceId) {
    if (sourceId == lastSourceId_) {
        return;
    }
    lastSourceId_ = sourceId;
    if (state_ == State::Ready || state_ == State::OnlyLocal) {
        rebuild();
        changed();
    }
}

void SourcesModel::setHiddenLanguages(std::vector<std::string> hidden) {
    if (hidden == hidden_) {
        return;
    }
    hidden_ = std::move(hidden);
    if (state_ == State::Ready || state_ == State::OnlyLocal) {
        rebuild();
        changed();
    }
}

void SourcesModel::rememberFocus(std::size_t row) {
    focusIndex_ = row;
    if (const core::Source* source = sourceAt(row)) {
        focusId_ = source->id;
        focusLastUsed_ = rows_[row].lastUsed;
    }
}

std::size_t SourcesModel::rememberedFocus() const {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const core::Source* source = sourceAt(i);
        if (source && source->id == focusId_ && rows_[i].lastUsed == focusLastUsed_) {
            return i;
        }
    }
    return rows_.empty() ? 0 : std::min(focusIndex_, rows_.size() - 1);
}

}  // namespace snx::ui
