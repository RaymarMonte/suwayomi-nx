#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>

#include "app/AppContext.hpp"
#include "core/models/Manga.hpp"
#include "ui/browse/LibraryAdder.hpp"

// The dialogs around LibraryAdder (WP-14, D56), the same wherever a manga is added or removed (the details screen, a source's
// Y menu, the Library's Y menu):
// - the same-title box "You have an entry in your library with the same name: ..." (Show entry / Add anyway / Cancel),
// - the category picker in choose mode (asked when Settings' "Ask for categories when adding" is on and the user has
//   categories; its "Don't ask again" turns that setting off),
// - the remove confirm "Remove <title> from your library? ..." (Cancel / Remove),
// - a notice when it is done or failed, and the library_add / library_remove test events.
// `done` gets the manga as it is now (inLibrary true after an add, with its categories; false after a remove): the caller
// updates its own screen and tells the library. Made with make(): the dialogs hold it weakly, so a screen that closes while
// a dialog is open just drops it. Main thread only.
namespace snx::ui {

class AddFlow : public std::enable_shared_from_this<AddFlow> {
  public:
    using Done = std::function<void(const core::Manga& now)>;

    static std::shared_ptr<AddFlow> make(app::AppContext& context, Done done);
    ~AddFlow();

    AddFlow(const AddFlow&) = delete;
    AddFlow& operator=(const AddFlow&) = delete;

    void add(const core::Manga& manga);     // the add flow; a notice if one is already under way
    void removeFromLibrary(const core::Manga& manga);  // the confirm box, then the remove
    bool busy() const { return adder_.busy(); }

  private:
    AddFlow(app::AppContext& context, Done done);
    void changed();
    void askDuplicate();
    void askCategories();
    void finished();
    void failed();

    app::AppContext& context_;
    LibraryAdder adder_;
    Done done_;
    LibraryAdder::Step shown_ = LibraryAdder::Step::Idle;  // the step the dialogs last acted on
    std::int64_t sentAtMs_ = 0;  // Add / Remove pressed (the app's clock), for the event's "ms"
};

}  // namespace snx::ui
