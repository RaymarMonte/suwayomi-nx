#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/LibraryTools.hpp"
#include "core/models/Category.hpp"
#include "net/CancelToken.hpp"

// The category picker's state (WP-24, D54): a checklist of the user's categories (Default isn't one: none checked means
// Default) for one manga, and "New category". Save sends only the difference the user made (core::categoryChange) in one
// updateMangaCategories. Without borealis, so it is unit-tested (test_ui_categories.cpp).
//
// WP-14 reuses it to choose the categories of a manga it adds to the library: start it with the categories to tick
// (`current`), let the user tick, and read chosen() instead of calling save().
namespace snx::ui {

class CategoryPickerModel {
  public:
    enum class State {
        Loading,  // the categories (and the manga's, when not given) are on their way
        Ready,    // rows() is the checklist (may be empty: no categories yet)
        Failed,   // nothing to show: error() says why, the screen offers Retry
        Saving,   // save() is under way
        Saved,    // save() is done: saved() is the manga's categories now
    };
    struct Row {
        core::Category category;
        bool checked = false;
    };

    // current: the manga's categories when the caller knows them (the details screen); nullopt = asked from the server
    // (manga(id)). nameMaxChars: RuntimeProfile::categoryNameMaxChars.
    CategoryPickerModel(api::SuwayomiClient& client, int mangaId, std::optional<std::vector<core::Category>> current,
                        std::size_t nameMaxChars);
    ~CategoryPickerModel();  // cancels what is under way

    CategoryPickerModel(const CategoryPickerModel&) = delete;
    CategoryPickerModel& operator=(const CategoryPickerModel&) = delete;

    void setListener(std::function<void()> listener);  // after every change (through the client's deliver)

    void start();  // Only once.
    void retry();  // after Failed

    State state() const { return state_; }
    const api::ApiError& error() const { return error_; }
    int mangaId() const { return mangaId_; }
    const std::vector<Row>& rows() const { return rows_; }  // the categories in the user's order, without Default
    bool busy() const { return state_ == State::Saving || adding_; }

    void toggle(std::size_t index);  // Ready only
    bool changed() const;            // the ticks differ from the start
    std::set<int> chosen() const;    // the ticked categories' ids (WP-14)

    // "New category": the same name rules as the Categories screen. The new one comes at the end, ticked.
    core::NameProblem checkName(const std::string& typed) const;
    bool addCategory(const std::string& typed);  // false when busy, not Ready, or the name has a problem

    // Sends the difference. Nothing changed: Saved at once, no request. False when not Ready or busy.
    bool save();
    const std::vector<core::Category>& saved() const { return saved_; }  // Saved: the manga's categories (from the server)
    bool savedChange() const { return savedChange_; }  // Saved: a request went out (the caller tells the library)

    // An action (save, New category) failed since the last call (empty afterwards). The list stays as it was.
    std::optional<api::ApiError> takeActionError();
    std::uint64_t revision() const { return revision_; }  // goes up whenever rows() changed

  private:
    void load();
    void loaded();  // both answers are in: the rows
    void changedNow();

    api::SuwayomiClient& client_;
    int mangaId_;
    std::optional<std::vector<core::Category>> given_;
    std::size_t nameMaxChars_;
    std::function<void()> listener_;
    net::CancelToken cancel_;
    bool started_ = false;
    State state_ = State::Loading;
    api::ApiError error_;
    std::optional<std::vector<core::Category>> categories_;  // every category (with Default), once in
    std::optional<std::set<int>> initial_;                   // the manga's categories at the start, once known
    int waiting_ = 0;  // answers still on their way
    bool failed_ = false;
    std::vector<Row> rows_;
    bool adding_ = false;
    std::vector<core::Category> saved_;
    bool savedChange_ = false;
    std::optional<api::ApiError> actionError_;
    std::uint64_t revision_ = 0;
};

}  // namespace snx::ui
