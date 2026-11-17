#include "ui/browse/SourceFiltersModel.hpp"

#include <utility>

namespace snx::ui {

namespace {

bool shown(core::FilterKind kind) {
    return kind != core::FilterKind::Separator && kind != core::FilterKind::Unknown;
}

}  // namespace

SourceFiltersModel::SourceFiltersModel(api::SuwayomiClient& client, std::string sourceId)
    : client_(client), sourceId_(std::move(sourceId)) {}

SourceFiltersModel::~SourceFiltersModel() {
    cancel_.cancel();
}

void SourceFiltersModel::setListener(std::function<void()> listener) {
    listener_ = std::move(listener);
}

void SourceFiltersModel::touched() {
    ++revision_;
    if (listener_) {
        listener_();
    }
}

void SourceFiltersModel::start() {
    if (started_) {
        return;
    }
    started_ = true;
    load();
}

void SourceFiltersModel::retry() {
    if (state_ != State::Failed) {
        return;
    }
    cancel_.cancel();
    cancel_ = net::CancelToken();
    load();
}

void SourceFiltersModel::load() {
    state_ = State::Loading;
    touched();
    const net::CancelToken token = cancel_;
    client_.sourceFilters(sourceId_, token)
        .then([this, token](const api::ApiResult<std::vector<core::SourceFilter>>& result) {
            if (token.cancelled()) {
                return;
            }
            if (!result.ok()) {
                error_ = result.error();
                state_ = State::Failed;
                touched();
                return;
            }
            filters_ = result.value();
            applied_ = core::defaultSettings(filters_);
            edit_ = applied_;
            open_.clear();
            state_ = State::Ready;
            rebuildRows();
            touched();
        });
}

void SourceFiltersModel::beginEdit() {
    edit_ = core::fitSettings(filters_, applied_);
    open_.clear();
    rebuildRows();
    touched();
}

void SourceFiltersModel::rebuildRows() {
    rows_.clear();
    for (std::size_t i = 0; i < filters_.size(); ++i) {
        const core::SourceFilter& filter = filters_[i];
        if (!shown(filter.kind)) {
            continue;  // they still count for the positions
        }
        rows_.push_back(Row{i, std::nullopt, 0});
        if (filter.kind == core::FilterKind::Group && isOpen(i)) {
            for (std::size_t c = 0; c < filter.children.size(); ++c) {
                if (shown(filter.children[c].kind) && filter.children[c].kind != core::FilterKind::Group) {
                    rows_.push_back(Row{i, c, 1});
                }
            }
        }
    }
}

const core::SourceFilter* SourceFiltersModel::filterAt(std::size_t row) const {
    if (row >= rows_.size()) {
        return nullptr;
    }
    const Row& r = rows_[row];
    const core::SourceFilter& filter = filters_[r.filter];
    if (r.child) {
        return *r.child < filter.children.size() ? &filter.children[*r.child] : nullptr;
    }
    return &filter;
}

const core::FilterValue* SourceFiltersModel::valueAt(std::size_t row) const {
    if (row >= rows_.size() || rows_[row].filter >= edit_.size()) {
        return nullptr;
    }
    const Row& r = rows_[row];
    const core::FilterSetting& setting = edit_[r.filter];
    if (r.child) {
        return *r.child < setting.children.size() ? &setting.children[*r.child] : nullptr;
    }
    return filters_[r.filter].kind == core::FilterKind::Group ? nullptr : &setting.value;
}

core::FilterValue* SourceFiltersModel::editValue(std::size_t row) {
    return const_cast<core::FilterValue*>(valueAt(row));
}

int SourceFiltersModel::groupChangedCount(std::size_t filterIndex) const {
    if (filterIndex >= filters_.size() || filters_[filterIndex].kind != core::FilterKind::Group ||
        filterIndex >= edit_.size()) {
        return 0;
    }
    // changedCount on a list of just this group.
    return core::changedCount({filters_[filterIndex]}, {edit_[filterIndex]});
}

void SourceFiltersModel::toggleGroup(std::size_t row) {
    const core::SourceFilter* filter = filterAt(row);
    if (!filter || filter->kind != core::FilterKind::Group || rows_[row].child) {
        return;
    }
    const std::size_t index = rows_[row].filter;
    if (!open_.erase(index)) {
        open_.insert(index);
    }
    rebuildRows();
    touched();
}

void SourceFiltersModel::setText(std::size_t row, std::string text) {
    const core::SourceFilter* filter = filterAt(row);
    core::FilterValue* value = editValue(row);
    if (!filter || !value || filter->kind != core::FilterKind::Text) {
        return;
    }
    value->text = std::move(text);
    touched();
}

void SourceFiltersModel::setSelect(std::size_t row, int index) {
    const core::SourceFilter* filter = filterAt(row);
    core::FilterValue* value = editValue(row);
    if (!filter || !value || filter->kind != core::FilterKind::Select || index < 0 ||
        static_cast<std::size_t>(index) >= filter->values.size()) {
        return;
    }
    value->select = index;
    touched();
}

void SourceFiltersModel::toggleCheck(std::size_t row) {
    const core::SourceFilter* filter = filterAt(row);
    core::FilterValue* value = editValue(row);
    if (!filter || !value || filter->kind != core::FilterKind::CheckBox) {
        return;
    }
    value->check = !value->check;
    touched();
}

void SourceFiltersModel::cycleTri(std::size_t row) {
    const core::SourceFilter* filter = filterAt(row);
    core::FilterValue* value = editValue(row);
    if (!filter || !value || filter->kind != core::FilterKind::TriState) {
        return;
    }
    value->tri = core::nextTriState(value->tri);
    touched();
}

void SourceFiltersModel::chooseSort(std::size_t row, int index) {
    const core::SourceFilter* filter = filterAt(row);
    core::FilterValue* value = editValue(row);
    if (!filter || !value || filter->kind != core::FilterKind::Sort || index < 0 ||
        static_cast<std::size_t>(index) >= filter->values.size()) {
        return;
    }
    if (value->sort && value->sort->index == index) {
        value->sort->ascending = !value->sort->ascending;
    } else {
        value->sort = core::SortSelection{index, false};
    }
    touched();
}

void SourceFiltersModel::reset() {
    edit_ = core::defaultSettings(filters_);
    touched();
}

void SourceFiltersModel::apply() {
    applied_ = edit_;
    touched();
}

}  // namespace snx::ui
