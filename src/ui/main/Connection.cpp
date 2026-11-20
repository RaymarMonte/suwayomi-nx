#include "ui/main/Connection.hpp"

#include <algorithm>

namespace snx::ui {

const char* Connection::stateName(State state) {
    switch (state) {
        case State::Unknown:
            return "unknown";
        case State::Online:
            return "online";
        case State::Offline:
            return "offline";
    }
    return "?";
}

const char* Connection::reasonName(Reason reason) {
    switch (reason) {
        case Reason::None:
            return "none";
        case Reason::NoNetwork:
            return "no_network";
        case Reason::CantReach:
            return "cant_reach";
    }
    return "?";
}

const char* Connection::byName(By by) {
    switch (by) {
        case By::Start:
            return "start";
        case By::Timer:
            return "timer";
        case By::Focus:
            return "focus";
        case By::Wake:
            return "wake";
        case By::Suspect:
            return "suspect";
        case By::Retry:
            return "retry";
        case By::Network:
            return "network";
    }
    return "?";
}

bool Connection::isOfflineError(const api::ApiError& error) {
    if (error.kind == api::ApiErrorKind::Offline) {
        return true;
    }
    // A proxy in front of a stopped server (Tailscale serve, nginx) answers these itself.
    return error.httpStatus == 502 || error.httpStatus == 503 || error.httpStatus == 504;
}

Connection::Options Connection::optionsFrom(const core::RuntimeProfile& profile) {
    Options options;
    options.checkMs = std::max(profile.offlineCheckMs, 1000);
    options.networkPollMs = std::max(profile.offlineNetworkPollMs, 100);
    options.suspectGapMs = std::max(profile.offlineSuspectGapMs, 0);
    return options;
}

Connection::Connection(Options options, Hooks hooks) : options_(options), hooks_(std::move(hooks)) {}

Connection::~Connection() {
    *alive_ = false;
}

void Connection::say(bool warning, const std::string& line) const {
    if (hooks_.log) {
        hooks_.log(warning, line);
    }
}

void Connection::setState(State state, Reason reason) {
    if (state == state_ && reason == reason_) {
        return;
    }
    state_ = state;
    reason_ = reason;
    say(state == State::Offline, std::string("connection: ") + stateName(state) +
                                     (state == State::Offline ? std::string(" (") + reasonName(reason) + ")" : ""));
    for (const auto& listener : std::vector(listeners_)) {  // a copy: a listener may remove itself
        listener.second(state_, reason_);
    }
}

bool Connection::beforeCheck(By by) {
    if (!hooks_.networkConnected()) {
        checking_ = false;
        setState(State::Offline, Reason::NoNetwork);
        if (hooks_.report) {
            hooks_.report(state_, reason_, by, 0);
        }
        scheduleTimers();
        return false;
    }
    checking_ = true;
    return true;
}

Connection::Verdict Connection::checked(By by, const api::ApiError* error, int ms) {
    checking_ = false;
    Verdict verdict = Verdict::Failed;
    if (error == nullptr) {
        setState(State::Online, Reason::None);
        verdict = Verdict::Online;
    } else if (isOfflineError(*error)) {
        setState(State::Offline, hooks_.networkConnected() ? Reason::CantReach : Reason::NoNetwork);
        verdict = Verdict::Offline;
    } else if (state_ == State::Online && (by == By::Suspect || by == By::Wake)) {
        say(false, std::string("connection: a ") + byName(by) + " check failed (" + api::apiErrorKindName(error->kind) +
                       ", " + error->detail + "), still online");
        verdict = Verdict::Ignored;
    } else {
        setState(State::Unknown, Reason::None);
    }
    say(false, std::string("connection: check by ") + byName(by) + " in " + std::to_string(ms) + " ms: " +
                   stateName(state_));
    if (hooks_.report) {
        hooks_.report(state_, reason_, by, ms);
    }
    scheduleTimers();
    return verdict;
}

void Connection::runCheck(By by) {
    if (checking_ || !check_) {
        return;
    }
    if (!hooks_.networkConnected()) {
        // No network: no request (curl would fail at once). Offline NoNetwork, and the poll waits for the network.
        const bool changed = state_ != State::Offline || reason_ != Reason::NoNetwork;
        setState(State::Offline, Reason::NoNetwork);
        if (changed && hooks_.report) {
            hooks_.report(state_, reason_, by, 0);
        }
        if (changed) {
            scheduleTimers();
        }
        return;
    }
    checking_ = true;
    check_(by);
}

void Connection::scheduleTimers() {
    const std::uint64_t ticket = ++timerTicket_;
    if (state_ != State::Offline) {
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    if (reason_ == Reason::NoNetwork) {
        hooks_.later(options_.networkPollMs, [this, alive, ticket] {
            if (!alive.expired()) {
                pollNetwork(ticket);
            }
        });
        return;
    }
    hooks_.later(options_.checkMs, [this, alive, ticket] {
        if (!alive.expired() && ticket == timerTicket_) {
            runCheck(By::Timer);
        }
    });
}

void Connection::pollNetwork(std::uint64_t ticket) {
    if (ticket != timerTicket_ || state_ != State::Offline) {
        return;
    }
    if (hooks_.networkConnected()) {
        say(false, "connection: the network is back");
        runCheck(By::Network);
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    hooks_.later(options_.networkPollMs, [this, alive, ticket] {
        if (!alive.expired()) {
            pollNetwork(ticket);
        }
    });
}

void Connection::focusBack() {
    if (state_ == State::Offline) {
        runCheck(By::Focus);
    }
}

void Connection::woke() {
    if (state_ != State::Unknown) {
        runCheck(By::Wake);
    }
}

void Connection::suspect() {
    if (suspectPosted_.exchange(true)) {
        return;
    }
    std::weak_ptr<bool> alive = alive_;
    hooks_.post([this, alive] {
        if (alive.expired()) {
            return;
        }
        suspectPosted_ = false;
        if (state_ != State::Online) {
            return;  // offline already (its own timers check), or not connected yet (the start check decides)
        }
        const std::int64_t now = hooks_.clockMs();
        if (anySuspectCheck_ && now - lastSuspectCheckMs_ < options_.suspectGapMs) {
            return;
        }
        anySuspectCheck_ = true;
        lastSuspectCheckMs_ = now;
        runCheck(By::Suspect);
    });
}

void Connection::reset() {
    ++timerTicket_;
    checking_ = false;
    setState(State::Unknown, Reason::None);
}

void Connection::noteServerDate(std::int64_t serverSeconds, std::int64_t consoleSeconds) {
    offset_.store(serverSeconds - consoleSeconds);
    offsetKnown_.store(true);
}

int Connection::addListener(Listener listener) {
    const int id = nextListener_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void Connection::removeListener(int id) {
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [id](const auto& l) { return l.first == id; }),
                     listeners_.end());
}

}  // namespace snx::ui
