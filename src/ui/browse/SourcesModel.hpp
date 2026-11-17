#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/SourceLanguages.hpp"
#include "core/models/Source.hpp"
#include "net/CancelToken.hpp"

// The Browse tab's list of sources (WP-14, D56): every source the server has, as rows by language (core::sourceRows: Last
// used, Local, then languages by name), without the languages the user hid. Without borealis, so it is unit-tested
// (test_ui_browse.cpp). MainActivity owns it (D42: the tab frame deletes the tab's view on a tab change); BrowseTab shows it.
// The Last used source and the hidden languages come from config.json (AppConfig::browse); the view saves them.
namespace snx::ui {

class SourcesModel {
  public:
    enum class State {
        Loading,    // the list is on its way
        Ready,      // rows() has sources
        OnlyLocal,  // the server has no source but Local: the screen says how to get some
        Failed,     // nothing to show: error() says why, the screen offers Retry
    };

    SourcesModel(api::SuwayomiClient& client, std::string lastSourceId, std::vector<std::string> hiddenLanguages);
    ~SourcesModel();  // cancels what is under way

    SourcesModel(const SourcesModel&) = delete;
    SourcesModel& operator=(const SourcesModel&) = delete;

    void setListener(std::function<void()> listener);  // after every change (through the client's deliver)

    void start();    // loads the list. Only once.
    void refresh();  // loads it again (X, Retry)

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const std::vector<core::Source>& sources() const { return sources_; }  // as the server sent them
    const std::vector<core::SourceRow>& rows() const { return rows_; }
    const core::Source* sourceAt(std::size_t row) const;  // nullptr for a wrong index
    // The languages that have sources, in the list's order (the Languages checklist).
    std::vector<std::string> languages() const { return core::sourceLanguages(sources_); }

    // A source was opened: it becomes Last used (the rows change).
    void opened(const std::string& sourceId);
    const std::string& lastSourceId() const { return lastSourceId_; }
    // The Languages checklist's Done.
    void setHiddenLanguages(std::vector<std::string> hidden);
    const std::vector<std::string>& hiddenLanguages() const { return hidden_; }

    std::uint64_t revision() const { return revision_; }  // goes up whenever rows() changed

    // The row that had the focus (by source id), so the list comes back to it.
    void rememberFocus(std::size_t row);
    std::size_t rememberedFocus() const;

  private:
    void load();
    void rebuild();
    void changed();

    api::SuwayomiClient& client_;
    std::string lastSourceId_;
    std::vector<std::string> hidden_;
    std::function<void()> listener_;
    net::CancelToken cancel_;
    bool started_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    std::vector<core::Source> sources_;
    std::vector<core::SourceRow> rows_;
    std::uint64_t revision_ = 0;
    std::string focusId_;
    bool focusLastUsed_ = false;
    std::size_t focusIndex_ = 0;
};

}  // namespace snx::ui
