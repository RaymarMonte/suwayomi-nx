#include "ui/browse/LanguagesModel.hpp"

#include <algorithm>
#include <utility>

#include "core/SourceLanguages.hpp"

namespace snx::ui {

namespace {

bool contains(const std::vector<std::string>& list, const std::string& code) {
    return std::find(list.begin(), list.end(), code) != list.end();
}

}  // namespace

LanguagesModel::LanguagesModel(const std::vector<std::string>& languages, std::vector<std::string> hidden) {
    for (const std::string& code : languages) {
        if (std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.code == code; })) {
            continue;  // each language once
        }
        Row row;
        row.code = code;
        row.name = core::languageName(code);
        row.shown = !contains(hidden, code);
        rows_.push_back(std::move(row));
    }
    for (const std::string& code : hidden) {
        const bool listed = std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.code == code; });
        if (!listed && !contains(kept_, code)) {
            kept_.push_back(code);
        }
    }
    if (!rows_.empty() && shownCount() == 0) {
        rows_.front().shown = true;  // every language hidden (a file edited by hand): the first one shows again
    }
    initial_ = this->hidden();
}

std::size_t LanguagesModel::shownCount() const {
    return static_cast<std::size_t>(std::count_if(rows_.begin(), rows_.end(), [](const Row& r) { return r.shown; }));
}

bool LanguagesModel::toggle(std::size_t index) {
    if (index >= rows_.size()) {
        return false;
    }
    if (rows_[index].shown && shownCount() <= 1) {
        return false;  // the last language shown stays
    }
    rows_[index].shown = !rows_[index].shown;
    return true;
}

std::vector<std::string> LanguagesModel::hidden() const {
    std::vector<std::string> out = kept_;
    for (const Row& row : rows_) {
        if (!row.shown) {
            out.push_back(row.code);
        }
    }
    return out;
}

}  // namespace snx::ui
