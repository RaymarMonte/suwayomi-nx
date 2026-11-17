#include "ui/browse/SourceActivity.hpp"

#include <algorithm>
#include <utility>

#include "app/ConfigStore.hpp"
#include "app/TestHooks.hpp"
#include "core/LibraryTools.hpp"
#include "ui/browse/AddFlow.hpp"
#include "ui/browse/FiltersActivity.hpp"
#include "ui/components/AskText.hpp"
#include "ui/components/CoverCell.hpp"
#include "ui/manga/MangaActivity.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

constexpr std::size_t kReportedTitles = 10;  // the "browse" event lists the first titles, for the scenario

bool inside(brls::View* view, const brls::View* parent) {
    for (; view != nullptr; view = view->getParent()) {
        if (view == parent) {
            return true;
        }
    }
    return false;
}

std::string displayName(const core::Source& source) {
    return source.displayName.empty() ? source.name : source.displayName;
}

}  // namespace

SourceActivity::SourceActivity(app::AppContext& context, core::Source source,
                               std::function<void(const core::Manga&)> onLibraryChanged)
    : context_(context),
      model_(context.client, source, static_cast<std::size_t>(std::max(1, context.profile.browseMaxItems))),
      filters_(context.client, source.id),
      onLibraryChanged_(std::move(onLibraryChanged)) {}

SourceActivity::~SourceActivity() {
    alive_.cancel();
    prefetch_.cancel();
    if (grid_ != nullptr) {
        grid_->setSource(nullptr);  // unbinds the cells (cancels their loads) while this screen still exists
    }
    model_.setListener({});
    filters_.setListener({});
}

void SourceActivity::onContentAvailable() {
    if (auto* frame = dynamic_cast<brls::AppletFrame*>(getContentView())) {
        frame->setTitle(displayName(model_.source()));
    }
    status_ = new StatusView();
    status_->setGrow(1.0f);
    body->addView(status_);
    grid_ = new CoverGrid(kCoverGridMetrics);
    grid_->setGrow(1.0f);
    grid_->setVisibility(brls::Visibility::GONE);
    body->addView(grid_);
    if (!model_.source().supportsLatest) {
        latestButton->setVisibility(brls::Visibility::GONE);
    }

    popularButton->registerClickAction([this](brls::View*) {
        setMode(SourceBrowseModel::Mode::Popular);
        return true;
    });
    latestButton->registerClickAction([this](brls::View*) {
        setMode(SourceBrowseModel::Mode::Latest);
        return true;
    });
    clearButton->registerClickAction([this](brls::View*) {
        model_.clearSearch();
        return true;
    });
    searchButton->registerClickAction([this](brls::View*) {
        askSearch();
        return true;
    });
    filtersButton->registerClickAction([this](brls::View*) {
        openFilters();
        return true;
    });
    refreshButton->registerClickAction([this](brls::View*) {
        refresh();
        return true;
    });
    brls::View* root = getContentView();
    root->registerAction("browse/mode"_i18n, brls::BUTTON_RB, [this](brls::View*) {
        setMode(SourceBrowseModel::Mode::Latest);
        return true;
    }, true);
    root->registerAction("browse/mode"_i18n, brls::BUTTON_LB, [this](brls::View*) {
        setMode(SourceBrowseModel::Mode::Popular);
        return true;
    }, true);
    root->registerAction("browse/cover_menu"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        if (gridShown_ && inside(brls::Application::getCurrentFocus(), grid_)) {
            openCoverMenu(grid_->focusIndex());
        }
        return true;
    });
    root->registerAction("browse/refresh"_i18n, brls::BUTTON_X, [this](brls::View*) {
        refresh();
        return true;
    });
    // PLUS quits the app everywhere, so the menu is on MINUS (D42).
    root->registerAction("browse/menu"_i18n, brls::BUTTON_BACK, [this](brls::View*) {
        openMenu();
        return true;
    });
    // B: a search shows → clear it (popular again); else back. Replaces the frame's own B (same view, same button).
    root->registerAction("browse/back"_i18n, brls::BUTTON_B, [this](brls::View*) {
        back();
        return true;
    });

    const net::CancelToken alive = alive_;
    flow_ = AddFlow::make(context_, [this, alive](const core::Manga& now) {
        if (!alive.cancelled()) {
            libraryChanged(now);
        }
    });
    ready_ = true;
    model_.setListener([this] { modelChanged(); });
    filters_.setListener([this] { updateBar(); });
    grid_->setSource(this);
    startedAtMs_ = app::appClockMs();
    model_.start();
    filters_.start();  // the filters are loaded when the screen opens (D56)
    modelChanged();
    reportScreen();
}

void SourceActivity::onResume() {
    updateBar();
    reportScreen();
}

void SourceActivity::reportScreen() {
    nlohmann::ordered_json params = nlohmann::ordered_json::object();
    params["id"] = model_.source().id;
    params["name"] = displayName(model_.source());
    app::testhooks::screen("Source", params);
}

void SourceActivity::back() {
    if (model_.mode() == SourceBrowseModel::Mode::Search) {
        // On the next frame: the bar's buttons change under the running action.
        const net::CancelToken alive = alive_;
        brls::sync([this, alive] {
            if (!alive.cancelled()) {
                model_.clearSearch();
            }
        });
        return;
    }
    brls::Application::popActivity();
}

void SourceActivity::setMode(SourceBrowseModel::Mode mode) {
    if (model_.mode() == SourceBrowseModel::Mode::Search) {
        return;  // the bar shows the search: Clear first
    }
    startedAtMs_ = app::appClockMs();
    model_.setMode(mode);
}

void SourceActivity::refresh() {
    startedAtMs_ = app::appClockMs();
    if (model_.moreFailed() || model_.state() == SourceBrowseModel::State::Failed) {
        model_.retry();  // the page that failed
    } else {
        model_.refresh();
    }
}

void SourceActivity::askSearch() {
    const int maxChars = context_.profile.searchMaxChars;
    askText(brls::getStr("browse/search_header", displayName(model_.source())), model_.query(), maxChars,
            [this, maxChars](std::string text) {
                const std::string trimmed = core::utf8Cut(core::trimText(text), static_cast<std::size_t>(maxChars));
                startedAtMs_ = app::appClockMs();
                model_.search(trimmed, filters_.appliedChanges(), filters_.appliedCount());
            });
}

void SourceActivity::openFilters() {
    if (filters_.state() == SourceFiltersModel::State::Failed) {
        filters_.retry();
    }
    const net::CancelToken alive = alive_;
    brls::Application::pushActivity(new FiltersActivity(
        context_, filters_, displayName(model_.source()),
        [this, alive] {
            if (!alive.cancelled()) {
                applyFilters();
            }
        },
        [this, alive] {
            if (!alive.cancelled()) {
                filters_.setListener([this] { updateBar(); });
            }
        }));
}

void SourceActivity::applyFilters() {
    startedAtMs_ = app::appClockMs();
    model_.search(model_.query(), filters_.appliedChanges(), filters_.appliedCount());
}

void SourceActivity::updateBar() {
    if (!ready_) {
        return;
    }
    const bool searching = model_.mode() == SourceBrowseModel::Mode::Search;
    popularButton->setStyle(model_.mode() == SourceBrowseModel::Mode::Popular ? &brls::BUTTONSTYLE_PRIMARY
                                                                              : &brls::BUTTONSTYLE_BORDERLESS);
    latestButton->setStyle(model_.mode() == SourceBrowseModel::Mode::Latest ? &brls::BUTTONSTYLE_PRIMARY
                                                                            : &brls::BUTTONSTYLE_BORDERLESS);
    const int applied = filters_.state() == SourceFiltersModel::State::Ready ? filters_.appliedCount() : 0;
    filtersButton->setText(applied > 0 ? brls::getStr("browse/filters_count", applied) : "browse/filters"_i18n);
    if (searching) {
        if (model_.query().empty()) {
            searchLabel->setText(brls::getStr("browse/filters_only", model_.filtersChanged()));
        } else if (model_.filtersChanged() > 0) {
            searchLabel->setText(brls::getStr("browse/search_label_filters", model_.query(), model_.filtersChanged()));
        } else {
            searchLabel->setText(brls::getStr("browse/search_label", model_.query()));
        }
    }
    if (searching == searchShown_) {
        return;
    }
    brls::View* focus = brls::Application::getCurrentFocus();
    const bool hadFocus = inside(focus, modes) || inside(focus, searchBox);
    searchShown_ = searching;
    // First the visibility, then the focus (a hidden view refuses it, D42).
    modes->setVisibility(searching ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    searchBox->setVisibility(searching ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    if (hadFocus) {
        brls::Application::giveFocus(searching ? static_cast<brls::View*>(clearButton) : popularButton);
    }
}

void SourceActivity::showGrid(bool visible) {
    if (visible == gridShown_) {
        return;
    }
    brls::View* focus = brls::Application::getCurrentFocus();
    // While the first page loads, the status can't take the focus, so borealis puts it on the bar's first button. When
    // the first covers show, the focus moves to them (a controller user starts on the covers, not on Popular).
    const bool startFocus = !gridEverShown_ && (focus == popularButton || focus == latestButton);
    const bool hadFocus = inside(focus, grid_) || inside(focus, status_) || (visible && startFocus);
    gridShown_ = visible;
    if (visible) {
        gridEverShown_ = true;
        status_->setVisibility(brls::Visibility::GONE);
        grid_->setVisibility(brls::Visibility::VISIBLE);
        if (hadFocus) {
            brls::Application::giveFocus(grid_);
        }
    } else {
        grid_->setVisibility(brls::Visibility::GONE);
        status_->setVisibility(brls::Visibility::VISIBLE);
        if (hadFocus) {
            brls::Application::giveFocus(status_);
        }
    }
}

void SourceActivity::modelChanged() {
    if (!ready_) {
        return;
    }
    updateBar();
    switch (model_.state()) {
        case SourceBrowseModel::State::Loading:
            status_->showLoading(brls::getStr("browse/loading", displayName(model_.source())));
            showGrid(false);
            shownCount_ = 0;
            break;
        case SourceBrowseModel::State::Failed:
            status_->showError("Source", model_.error(), [this] { refresh(); });
            showGrid(false);
            shownCount_ = 0;
            break;
        case SourceBrowseModel::State::Empty:
            if (model_.mode() == SourceBrowseModel::Mode::Search) {
                status_->showMessage("browse/nothing_found"_i18n, "browse/nothing_found_text"_i18n);
            } else {
                status_->showMessage("browse/nothing_listed"_i18n, "browse/nothing_listed_text"_i18n);
            }
            showGrid(false);
            shownCount_ = 0;
            break;
        case SourceBrowseModel::State::Ready: {
            const std::size_t count = model_.items().size();
            showGrid(true);
            // shownCount_ 0: the grid showed nothing of this list yet (it was loading: the list may be a new one).
            if (shownCount_ == 0 || model_.listRevision() != shownList_ || count < shownCount_) {
                grid_->reload(model_.rememberedFocus());
            } else if (count > shownCount_) {
                grid_->appended();
            } else if (model_.itemsRevision() != shownItems_) {
                grid_->rebind();  // an "In library" tag changed
            }
            shownCount_ = count;
            break;
        }
    }
    shownList_ = model_.listRevision();
    shownItems_ = model_.itemsRevision();
    if (model_.moreFailed() && !moreFailedShown_) {
        brls::Application::notify("browse/more_failed"_i18n);
    }
    moreFailedShown_ = model_.moreFailed();
    report();
}

void SourceActivity::report() {
    if (model_.state() == SourceBrowseModel::State::Loading || model_.loadingMore()) {
        return;
    }
    static const char* const kStates[] = {"loading", "ready", "empty", "failed"};
    nlohmann::ordered_json titles = nlohmann::ordered_json::array();
    nlohmann::ordered_json inLibrary = nlohmann::ordered_json::array();
    for (std::size_t i = 0; i < model_.items().size(); ++i) {
        const core::Manga& m = model_.items()[i];
        if (i < kReportedTitles) {
            titles.push_back(m.title);
        }
        if (m.inLibrary) {
            inLibrary.push_back(m.id);
        }
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["source"] = model_.source().id;
    fields["mode"] = std::string(browseModeName(model_.mode()));
    fields["query"] = model_.query();
    fields["filters"] = model_.filtersChanged();
    fields["state"] = kStates[static_cast<int>(model_.state())];
    fields["items"] = model_.items().size();
    fields["hasNext"] = model_.hasNext();
    fields["pages"] = model_.pagesLoaded();
    fields["moreFailed"] = model_.moreFailed();
    fields["titles"] = titles;
    fields["inLibrary"] = inLibrary;
    if (model_.state() == SourceBrowseModel::State::Failed) {
        fields["kind"] = api::apiErrorKindName(model_.error().kind);
        fields["message"] = errorText(model_.error());
    }
    const std::string key = fields.dump();
    if (key != reported_) {
        reported_ = key;
        fields["ms"] = app::appClockMs() - startedAtMs_;  // the page asked → on screen (not part of the "same" check)
        app::testhooks::write("browse", fields);
    }
}

std::size_t SourceActivity::count() const {
    return model_.state() == SourceBrowseModel::State::Ready ? model_.items().size() : 0;
}

GridCell* SourceActivity::makeCell() {
    return new CoverCell();
}

void SourceActivity::bind(GridCell& cell, std::size_t index) {
    const core::Manga& manga = model_.items()[index];
    cell.setId("source/cover-" + std::to_string(index));  // named in "focus" test events
    const std::string url = manga.thumbnailUrl.empty() ? std::string() : context_.client.absoluteUrl(manga.thumbnailUrl);
    static_cast<CoverCell&>(cell).show(manga, url, context_, manga.inLibrary ? "browse/in_library"_i18n : std::string());
}

void SourceActivity::unbind(GridCell& cell) {
    static_cast<CoverCell&>(cell).clear();
}

void SourceActivity::focused(std::size_t index) {
    model_.rememberFocus(index);
}

void SourceActivity::showing(std::size_t first, std::size_t last) {
    if (last == 0) {
        return;
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["first"] = first;
    fields["last"] = last;
    fields["columns"] = GridLayout(grid_->getWidth(), kCoverGridMetrics).columns();
    app::testhooks::write("grid", fields);
    if (!model_.loadingMore() && model_.hasNext()) {
        startedAtMs_ = app::appClockMs();
    }
    model_.showing(last - 1);  // near the end of what is loaded: the next page
    prefetch_.cancel();
    prefetch_ = net::CancelToken();
    const auto columns = static_cast<std::size_t>(GridLayout(grid_->getWidth(), kCoverGridMetrics).columns());
    prefetchCovers(context_, model_.items(), last, columns, prefetch_);
}

void SourceActivity::select(std::size_t index) {
    if (index >= model_.items().size()) {
        return;
    }
    model_.rememberFocus(index);
    core::Manga manga = model_.items()[index];
    if (!manga.source) {
        manga.source = model_.source();  // "Loading from <source>..." on the preview
    }
    const net::CancelToken alive = alive_;
    brls::Application::pushActivity(new MangaActivity(context_, manga, {}, {}, [this, alive](const core::Manga& now) {
        if (!alive.cancelled()) {
            libraryChanged(now);
        }
    }));
}

void SourceActivity::libraryChanged(const core::Manga& now) {
    model_.setInLibrary(now.id, now.inLibrary);  // the "In library" tag
    if (onLibraryChanged_) {
        onLibraryChanged_(now);
    }
}

void SourceActivity::openMenu() {
    std::vector<std::string> entries = {"browse/menu_popular"_i18n};
    if (model_.source().supportsLatest) {
        entries.push_back("browse/menu_latest"_i18n);
    }
    entries.push_back("browse/menu_search"_i18n);
    entries.push_back("browse/menu_filters"_i18n);
    entries.push_back("browse/menu_refresh"_i18n);
    const bool latest = model_.source().supportsLatest;
    auto* dropdown = new brls::Dropdown("browse/menu_title"_i18n, entries, [this, latest](int selected) {
        // The menu closes first; the next one opens on the following frame.
        const int choice = !latest && selected >= 1 ? selected + 1 : selected;  // the same numbers with or without Latest
        const net::CancelToken alive = alive_;
        brls::sync([this, alive, choice] {
            if (alive.cancelled()) {
                return;
            }
            if (choice == 0 || choice == 1) {
                if (model_.mode() == SourceBrowseModel::Mode::Search) {
                    model_.clearSearch();
                }
                setMode(choice == 0 ? SourceBrowseModel::Mode::Popular : SourceBrowseModel::Mode::Latest);
            } else if (choice == 2) {
                askSearch();
            } else if (choice == 3) {
                openFilters();
            } else if (choice == 4) {
                refresh();
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void SourceActivity::openCoverMenu(std::size_t index) {
    if (index >= model_.items().size()) {
        return;
    }
    model_.rememberFocus(index);
    const core::Manga manga = model_.items()[index];
    const std::vector<std::string> entries = {"browse/open"_i18n,
                                              manga.inLibrary ? "browse/cover_remove"_i18n : "browse/cover_add"_i18n};
    const net::CancelToken alive = alive_;
    auto* dropdown = new brls::Dropdown(manga.title, entries, [this, alive, manga](int selected) {
        brls::sync([this, alive, manga, selected] {
            if (alive.cancelled()) {
                return;
            }
            if (selected == 0) {
                for (std::size_t i = 0; i < model_.items().size(); ++i) {
                    if (model_.items()[i].id == manga.id) {
                        select(i);
                        return;
                    }
                }
            } else if (selected == 1) {
                if (manga.inLibrary) {
                    flow_->removeFromLibrary(manga);
                } else {
                    flow_->add(manga);
                }
            }
        });
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

}  // namespace snx::ui
