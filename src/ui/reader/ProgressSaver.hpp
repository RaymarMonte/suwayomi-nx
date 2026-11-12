#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/ProgressSync.hpp"
#include "core/models/ReadProgress.hpp"
#include "net/CancelToken.hpp"

// Sends a reader's progress updates (WP-11, D44; shared by the page reader and the webtoon strip since WP-13b). Shared with
// the requests it starts, so a save that is on its way when the reader closes finishes (and starts the next one, if the
// reader moved on meanwhile) after the model is gone. Saves are never cancelled: an update cut off half way might or might
// not have reached the server. A progress with a spot (the strip) goes out as UpdateStripProgress (page + meta note in one
// request). Main thread only (the client's deliver). No borealis: unit-tested through ReaderModel and StripModel.
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
    void tidy();
};

}  // namespace snx::ui
