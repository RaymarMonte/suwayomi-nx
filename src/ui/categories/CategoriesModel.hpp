#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/LibraryTools.hpp"
#include "core/models/Category.hpp"
#include "net/CancelToken.hpp"

// The Categories screen's state (WP-24, D54): the user's categories with their manga counts, and the actions on them
// (add, rename, move up / down, delete). Without borealis, so it is unit-tested (FakeHttpClient, test_ui_categories.cpp).
// Default (id 0) is never listed nor changed. One action at a time; the list shows what the server answered.
// The server checks almost nothing (D54), so names go through checkName() first: the actions refuse a name it refuses.
namespace snx::ui {

class CategoriesModel {
  public:
    enum class State {
        Loading,  // the list is on its way
        Ready,    // items() has categories
        Empty,    // only Default: the user has no categories yet
        Failed,   // nothing to show: error() says why, the screen offers Retry
    };
    enum class Action { Add, Rename, Move, Delete };

    // A finished action (for the screen's message and the "category_action" test event).
    struct Done {
        Action action = Action::Add;
        bool ok = false;
        int categoryId = 0;
    };

    // nameMaxChars: RuntimeProfile::categoryNameMaxChars.
    CategoriesModel(api::SuwayomiClient& client, std::size_t nameMaxChars);
    ~CategoriesModel();  // cancels what is under way

    CategoriesModel(const CategoriesModel&) = delete;
    CategoriesModel& operator=(const CategoriesModel&) = delete;

    void setListener(std::function<void()> listener);  // after every change (through the client's deliver)

    void start();  // loads the list. Only once.
    void retry();  // after Failed

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    const std::vector<core::Category>& items() const { return items_; }  // in order, without Default
    bool busy() const { return busy_; }  // an action is under way

    // The problem with `typed` (trimmed here) as a new name (index: items() index of the one renamed; -1 = a new one).
    core::NameProblem checkName(const std::string& typed, long index = -1) const;
    // Each one false (and no request) when busy, not Ready / Empty, the index is wrong, the name has a problem, or
    // nothing would change (the same name; a move past the end).
    bool add(const std::string& typed);
    bool renameAt(std::size_t index, const std::string& typed);
    bool move(std::size_t index, int step);  // -1 up, +1 down
    bool deleteAt(std::size_t index);

    // Something changed on the server since the screen opened (the Library loads everything again when it closes).
    bool changedAnything() const { return changedAnything_; }
    // The action that finished since the last call (empty afterwards), and its error when it failed.
    std::optional<Done> takeDone();
    std::optional<api::ApiError> takeActionError();
    // Goes up whenever items() changed: the view reloads its list.
    std::uint64_t revision() const { return revision_; }

    // The row that should have the focus: the one the user worked on (a new one, the moved one), by id.
    void rememberFocus(std::size_t index);
    std::size_t rememberedFocus() const;

  private:
    void load();
    void setAll(std::vector<core::Category> all);  // keeps the counts it knew for categories the answer has none for
    void finish(Action action, int categoryId, const api::ApiError* error);
    void changed();

    api::SuwayomiClient& client_;
    std::size_t nameMaxChars_;
    std::function<void()> listener_;
    net::CancelToken cancel_;
    bool started_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    std::vector<core::Category> all_;    // with Default, for the name check
    std::vector<core::Category> items_;  // without Default
    bool busy_ = false;
    bool changedAnything_ = false;
    std::optional<Done> done_;
    std::optional<api::ApiError> actionError_;
    std::uint64_t revision_ = 0;
    int focusId_ = 0;
    std::size_t focusIndex_ = 0;
};

}  // namespace snx::ui
