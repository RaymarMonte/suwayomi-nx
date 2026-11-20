#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api/ApiError.hpp"
#include "core/RuntimeProfile.hpp"

// Online or offline (WP-18b, D61): one shared state the screens read (AppContext::connection), and when the server is
// checked again. MainActivity runs every check (checkServer) and reports its result here; this decides what the result
// means and when the next check runs (through the `check` hook MainActivity sets). No borealis: the clock, timers, main
// thread, network status, log and test event come in as hooks (like WaitingSaves). Unit-tested:
// tests/unit/test_ui_connection.cpp.
//
// - At start (and Retry, a changed server): no network (networkConnected() false) -> Offline NoNetwork at once, no request;
//   else the check: it worked -> Online; it failed for want of the server -> Offline CantReach; any other failure (the
//   server is there but answered an error, a login that doesn't work) -> Unknown (the main screen shows the error or the
//   login, as before WP-18b).
// - What counts as "for want of the server": Offline (no answer: wrong address, server stopped, Wi-Fi off; a connect
//   timeout is one, D45), and HTTP 502 / 503 / 504 (a proxy in front of a stopped server). A Timeout after the request
//   went out is the server being slow: not offline.
// - Offline: a check every checkMs (30 s) and at once when the app gets the focus back or the console wakes; with no
//   network, networkConnected() every networkPollMs (2 s) and a check as soon as it says true. The first check that works
//   -> Online, the listeners hear it once.
// - Online: a request to the server that got no answer (net's response observer, any thread: suspect()) is only a
//   suspicion; it checks the server (at most once per suspectGapMs). Waking from sleep checks too. Only a check that fails
//   for want of the server goes Offline; another failure of such a background check changes nothing.
// - The server's clock: every answer's Date header (core::parseHttpDate) gives the offset server - console (seconds, the
//   newest answer wins; any thread). The waiting saves' "later reading wins" rule compares through it.
// Main thread only, except suspect(), noteServerDate() and clockOffset().
namespace snx::ui {

class Connection {
  public:
    enum class State {
        Unknown,  // not set up, checking for the first time, or the server answered with an error (not offline)
        Online,
        Offline,
    };
    enum class Reason { None, NoNetwork, CantReach };
    // What started a check (the "connection" test event's `by`).
    enum class By { Start, Timer, Focus, Wake, Suspect, Retry, Network };
    static const char* stateName(State state);    // "unknown", "online", "offline"
    static const char* reasonName(Reason reason);  // "none", "no_network", "cant_reach"
    static const char* byName(By by);              // "start", "timer", "focus", "wake", "suspect", "retry", "network"

    // An error that means the server can't be reached (see above).
    static bool isOfflineError(const api::ApiError& error);

    struct Options {
        int checkMs = 30000;       // RuntimeProfile::offlineCheckMs
        int networkPollMs = 2000;  // RuntimeProfile::offlineNetworkPollMs
        int suspectGapMs = 5000;   // RuntimeProfile::offlineSuspectGapMs
    };
    static Options optionsFrom(const core::RuntimeProfile& profile);

    struct Hooks {
        std::function<std::int64_t()> clockMs;                     // a steady clock (the app: appClockMs). Required
        std::function<void(int ms, std::function<void()>)> later;  // fn on the main thread after ms (brls::delay). Required
        std::function<void(std::function<void()>)> post;           // fn on the main thread soon, from any thread. Required
        std::function<bool()> networkConnected;                    // platform::networkConnected (tests: a switch). Required
        std::function<void(bool warning, const std::string& line)> log;
        // After every check (and an offline start without a request): the "connection" test event.
        std::function<void(State state, Reason reason, By by, int ms)> report;
    };

    Connection(Options options, Hooks hooks);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // MainActivity's check: it runs checkServer and calls checked() with the result (also for checks it starts itself).
    void setCheck(std::function<void(By)> check) { check_ = std::move(check); }

    // MainActivity is about to check (start, Retry, a changed server). False when there is no network: then the state is
    // Offline NoNetwork now (no request; the caller shows it). True: run the check and report it with checked(). A check
    // under way is superseded (its result, if it comes, is the caller's to drop).
    bool beforeCheck(By by);

    // What a finished check means for the main screen.
    enum class Verdict {
        Online,   // it worked
        Offline,  // the server can't be reached
        Failed,   // the server answered with an error: the main screen's error, login or setup flow
        Ignored,  // a background check while online failed with something else: nothing changes
    };
    // error: nullptr when the check worked. ms: how long it took (the event).
    Verdict checked(By by, const api::ApiError* error, int ms);

    // The app got the focus back (HOME, an applet on top): offline, a check now.
    void focusBack();
    // The console woke from sleep: a check now, online or not (the network may differ).
    void woke();
    // A request to the server got no answer (net's observer). Any thread: it only posts once to the main thread.
    void suspect();
    // The setup screen saved another server: Unknown until its check (no timers run meanwhile).
    void reset();

    State state() const { return state_; }
    Reason reason() const { return reason_; }
    bool online() const { return state_ == State::Online; }
    bool offline() const { return state_ == State::Offline; }
    bool checking() const { return checking_; }

    // The server's clock (any thread). serverSeconds: an answer's Date header; consoleSeconds: the console's clock then.
    void noteServerDate(std::int64_t serverSeconds, std::int64_t consoleSeconds);
    std::int64_t clockOffset() const { return offset_.load(); }  // server - console, 0 = unknown
    bool clockKnown() const { return offsetKnown_.load(); }

    // Every change of online / offline (main thread). Returns an id for removal.
    using Listener = std::function<void(State state, Reason reason)>;
    int addListener(Listener listener);
    void removeListener(int id);

  private:
    void runCheck(By by);  // through check_, unless one is under way
    void setState(State state, Reason reason);
    void scheduleTimers();  // the offline check and the network poll, by the state
    void pollNetwork(std::uint64_t ticket);
    void say(bool warning, const std::string& line) const;

    Options options_;
    Hooks hooks_;
    std::function<void(By)> check_;
    State state_ = State::Unknown;
    Reason reason_ = Reason::None;
    bool checking_ = false;
    std::uint64_t timerTicket_ = 0;  // only the newest timers run (a state change or a check makes new ones)
    std::int64_t lastSuspectCheckMs_ = 0;
    bool anySuspectCheck_ = false;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextListener_ = 1;
    std::atomic<bool> suspectPosted_{false};
    std::atomic<std::int64_t> offset_{0};
    std::atomic<bool> offsetKnown_{false};
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace snx::ui
