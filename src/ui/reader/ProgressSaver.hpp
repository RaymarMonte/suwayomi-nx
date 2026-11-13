#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/ProgressSync.hpp"
#include "core/models/ReadProgress.hpp"
#include "net/CancelToken.hpp"
#include "ui/reader/WaitingSaves.hpp"

// Sends a reader's progress updates (WP-11, D44; shared by the page reader and the webtoon strip since WP-13b). Shared with
// the requests it starts, so a save that is on its way when the reader closes finishes (and starts the next one, if the
// reader moved on meanwhile) after the model is gone. Saves are never cancelled: an update cut off half way might or might
// not have reached the server. A progress with a spot (the strip) goes out as UpdateStripProgress (page + meta note in one
// request). Main thread only (the client's deliver). No borealis: unit-tested through ReaderModel and StripModel.
// WP-15b: a save that fails after the reader began closing goes to the waiting-save queue (`queue`), with the base the
// chapter's ProgressSync kept, so it is sent once the server answers again (WaitingSaves).
namespace snx::ui {

struct ProgressSaver {
    api::SuwayomiClient& client;
    std::function<std::int64_t()> clock;
    std::function<void(const core::ReadProgress&, bool)> onSaved;  // every save, ok or not (the "progress" test event)
    std::function<void(const core::ReadProgress&)> toModel;         // the model, while it exists
    std::vector<std::shared_ptr<core::ProgressSync>> left;          // chapters left behind with something not saved yet
    int inFlight = 0;
    bool closing = false;  // no more retries: send what's left once, then call whenDone
    std::vector<std::function<void()>> whenDone;
    net::CancelToken never;
    // WP-15b: where a save that fails while closing goes (nullptr: nowhere, it is lost as before; it must outlive this),
    // and what the entry says about its manga and chapter (the log, History's note).
    WaitingSaves* queue = nullptr;
    int mangaId = 0;
    std::string mangaTitle;
    std::map<int, std::string> chapterNames;  // the chapters opened (the model adds each one it opens)

    ProgressSaver(api::SuwayomiClient& c, std::function<std::int64_t()> time,
                  std::function<void(const core::ReadProgress&, bool)> saved);

    static void send(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync,
                     const core::ReadProgress& progress);
    // Sends what is due (now: without waiting for the interval).
    static void flush(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync, bool now);
    static void flushLeft(const std::shared_ptr<ProgressSaver>& self, bool now);
    // A chapter the reader leaves: everything of it goes now, and it is kept until saved.
    static void leave(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync);
    // The reader closes: what is left goes once, then `done` runs once every save has ended (at once if none is on its way).
    static void close(const std::shared_ptr<ProgressSaver>& self, const std::shared_ptr<core::ProgressSync>& sync,
                      std::function<void()> done);
    // A save of `sync` failed while closing: what it hasn't saved goes to the queue.
    void handOff(const core::ProgressSync& sync);
    void tidy();
};

}  // namespace snx::ui
