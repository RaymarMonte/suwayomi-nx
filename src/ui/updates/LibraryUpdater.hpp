#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/SuwayomiClient.hpp"
#include "core/UpdatesHistory.hpp"
#include "core/models/LibraryUpdate.hpp"
#include "net/CancelToken.hpp"

// The Updates tab's "Update library" (WP-15), without borealis so it is unit-tested (FakeHttpClient, a played clock).
// MainActivity owns it, so an update keeps being followed whichever tab or screen is shown. It is the first screen part
// that polls a job on the server; WP-17's downloads copy the pattern.
//
// Start: read the status (its lastUpdateTimestamp is the "before"), send updateLibrary, then poll the status every
// `pollMs` until core::LibraryUpdateWatch says done (or nothing to update). Stop: updateStop, then the same rule. Polling
// runs only while an update is followed; check() polls once (when the tab first opens) so an update started elsewhere
// shows its progress too. A failed poll (server away) says so and waits `retryMs`, doubled per failure up to
// `retryMaxMs` (D45); the update runs on the server anyway. When done: one more status read with the manga that ran (the
// failed ones), and the first page of Updates counts the new chapters this update found.
namespace snx::ui {

class LibraryUpdater {
  public:
    enum class Phase {
        Idle,       // nothing followed: lastStatus() tells when the last update ran (once known)
        Starting,   // asked for, not seen running yet
        Running,    // progress: lastStatus().finished / total
        Finished,   // summary() is there
    };
    struct Summary {
        bool nothingToUpdate = false;  // the server didn't start one (the start wait ran out)
        bool stopped = false;
        int newChapters = 0;  // in the first Updates page: chapters this update found (fetched since it started)
        int skippedManga = 0;
        int total = 0;
        std::vector<std::string> failed;  // titles (logged)
        std::int64_t ms = 0;  // from the tap to done, by the app's clock
    };
    struct Limits {
        int pollMs = 1000;        // RuntimeProfile::libraryUpdatePollMs
        int startWaitMs = 10000;  // RuntimeProfile::libraryUpdateStartWaitMs
        int retryMs = 5000;       // RuntimeProfile::progressSyncMs
        int retryMaxMs = 60000;   // RuntimeProfile::progressRetryMaxMs
    };
    using Clock = std::function<std::int64_t()>;                         // the app's clock, milliseconds
    using Schedule = std::function<void(int ms, std::function<void()>)>;  // run fn on the main thread after ms (brls::delay)

    LibraryUpdater(api::SuwayomiClient& client, Limits limits, Clock clock, Schedule schedule);
    ~LibraryUpdater();  // cancels what is under way; a scheduled poll then does nothing
    LibraryUpdater(const LibraryUpdater&) = delete;
    LibraryUpdater& operator=(const LibraryUpdater&) = delete;

    // Called after every change (through the client's deliver / the schedule: the main thread in the app). Several
    // listeners: the Updates tab's view and MainActivity. The int it returns removes it again.
    int addListener(std::function<void()> listener);
    void removeListener(int id);

    void check();  // one status read (the tab opened): shows "Last updated", follows an update that already runs
    bool start();  // Update library. False while one is followed.
    bool stop();   // Stop. False when nothing runs or a stop was already sent.

    Phase phase() const { return phase_; }
    bool active() const { return phase_ == Phase::Starting || phase_ == Phase::Running; }
    bool stopping() const { return stopSent_; }
    // The last status the server sent (nullopt before the first one).
    const std::optional<core::LibraryUpdateStatus>& lastStatus() const { return status_; }
    bool pollFailed() const { return pollFailed_; }  // the last poll found no server ("Can't reach the server")
    const api::ApiError& pollError() const { return pollError_; }
    const std::optional<Summary>& summary() const { return summary_; }  // after Finished
    // An error starting or stopping (empty afterwards): the view tells the user.
    std::optional<api::ApiError> takeError();
    // Goes up once per finished update (the view and MainActivity reload what they show).
    std::uint64_t finishedCount() const { return finished_; }

  private:
    void poll();
    void onPoll(const api::ApiResult<core::LibraryUpdateStatus>& result);
    void schedulePoll(int ms);
    void finish(core::UpdatePhase phase);
    void changed();

    api::SuwayomiClient& client_;
    Limits limits_;
    Clock clock_;
    Schedule schedule_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // scheduled polls check it
    net::CancelToken cancel_;
    std::vector<std::pair<int, std::function<void()>>> listeners_;
    int nextListener_ = 1;
    Phase phase_ = Phase::Idle;
    std::optional<core::LibraryUpdateWatch> watch_;
    std::optional<core::LibraryUpdateStatus> status_;
    std::optional<Summary> summary_;
    std::optional<api::ApiError> pendingError_;
    api::ApiError pollError_;
    bool pollFailed_ = false;
    bool polling_ = false;   // a poll is on its way or scheduled
    bool checking_ = false;  // check()'s read is on its way
    bool stopSent_ = false;
    int failures_ = 0;
    std::int64_t tappedAt_ = 0;
    std::uint64_t finished_ = 0;
};

}  // namespace snx::ui
