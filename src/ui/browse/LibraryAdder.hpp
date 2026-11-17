#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/models/Category.hpp"
#include "core/models/Manga.hpp"
#include "net/CancelToken.hpp"

// Adding a manga to the library and taking it out (WP-14, D56), for the preview and the Y menus (Browse, Library). Without
// borealis, so it is unit-tested (test_ui_browse.cpp). The view shows the questions; this model asks the server.
//
// add():  1. the same-title check (libraryDuplicates): a match stops at AskDuplicate (Show entry / Add anyway / Cancel);
//         2. the categories: with askCategories and categories besides Default it stops at AskCategories (the picker in
//            choose mode, the server's "default"-marked categories pre-ticked); otherwise those, or none (= Default);
//         3. a manga the server has no details for yet (initialized false) is fetched from its source first;
//         4. addToLibrary (inLibrary + exactly the chosen categories, one request) -> Done.
// removeFromLibrary(): the server call -> Done. One flow at a time; cancel() stops one waiting for an answer from the user.
namespace snx::ui {

class LibraryAdder {
  public:
    enum class Step {
        Idle,           // nothing under way
        Checking,       // the same-title check and the categories are on their way
        AskDuplicate,   // duplicate() has the same title: the view asks
        AskCategories,  // the view opens the picker with preticked()
        Adding,         // fetching details and adding
        Removing,
        Done,           // result() is the manga now (in or out of the library)
        Failed,         // error() says why
    };

    explicit LibraryAdder(api::SuwayomiClient& client);
    ~LibraryAdder();  // cancels what is under way

    LibraryAdder(const LibraryAdder&) = delete;
    LibraryAdder& operator=(const LibraryAdder&) = delete;

    void setListener(std::function<void()> listener);

    // False (nothing starts) while a flow is under way.
    bool add(core::Manga manga, bool askCategories);
    bool removeFromLibrary(core::Manga manga);
    void addAnyway();                         // at AskDuplicate
    void chooseCategories(std::set<int> ids);  // at AskCategories: the picker's Add (none = Default)
    void cancel();                            // at AskDuplicate / AskCategories (or while waiting): back to Idle

    Step step() const { return step_; }
    bool busy() const { return step_ != Step::Idle && step_ != Step::Done && step_ != Step::Failed; }
    bool removing() const { return removing_; }  // the flow is a remove
    const core::Manga& manga() const { return manga_; }    // the manga of the flow
    const core::Manga* duplicate() const { return duplicate_ ? &*duplicate_ : nullptr; }
    const std::vector<core::Category>& categories() const { return categories_; }  // with Default
    std::set<int> preticked() const;  // the "default"-marked categories, never Default itself
    bool asked() const { return asked_; }  // the picker was shown in this flow
    bool sawDuplicate() const { return sawDuplicate_; }
    const core::Manga& result() const { return result_; }
    const api::ApiError& error() const { return error_; }

  private:
    void checked();  // both answers of step 1 are in
    void decideCategories();
    void addWith(std::vector<int> ids);
    void send(std::vector<int> ids);
    void fail(const api::ApiError& error);
    void finish(core::Manga result);
    void changed();

    api::SuwayomiClient& client_;
    std::function<void()> listener_;
    net::CancelToken cancel_;
    Step step_ = Step::Idle;
    bool removing_ = false;
    bool askCategories_ = true;
    core::Manga manga_;
    std::optional<core::Manga> duplicate_;
    std::vector<core::Category> categories_;
    int waiting_ = 0;
    std::optional<api::ApiError> firstError_;
    bool asked_ = false;
    bool sawDuplicate_ = false;
    core::Manga result_;
    api::ApiError error_;
};

std::string_view adderStepName(LibraryAdder::Step step);  // "idle", "checking", ... (test events, logs)

}  // namespace snx::ui
