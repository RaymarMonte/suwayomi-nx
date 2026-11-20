// WP-18b (D61): ui::Connection, online or offline. No network -> offline without a request; a check that can't reach the
// server -> offline (502 too), any other failure -> the main screen's error; the 30 s timer, focus and wake checks; the
// network poll; a suspicion checks at most once per gap; back online tells the listeners once; the clock offset; timers
// that fire after it is gone do nothing.
#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api/ApiError.hpp"
#include "ui/main/Connection.hpp"

using namespace snx;
using ui::Connection;

namespace {

struct Harness {
    std::int64_t now = 1000;
    bool network = true;
    std::vector<std::pair<int, std::function<void()>>> timers;
    std::vector<std::function<void()>> posts;
    std::vector<Connection::By> checks;  // the checks the connection asked for
    std::vector<std::string> events;     // "state/reason/by"
    std::vector<std::string> heard;      // listeners: "state/reason"
    std::unique_ptr<Connection> connection;

    Harness() {
        Connection::Hooks hooks;
        hooks.clockMs = [this] { return now; };
        hooks.later = [this](int ms, std::function<void()> fn) { timers.emplace_back(ms, std::move(fn)); };
        hooks.post = [this](std::function<void()> fn) { posts.push_back(std::move(fn)); };
        hooks.networkConnected = [this] { return network; };
        hooks.report = [this](Connection::State s, Connection::Reason r, Connection::By by, int) {
            events.push_back(std::string(Connection::stateName(s)) + "/" + Connection::reasonName(r) + "/" +
                             Connection::byName(by));
        };
        connection = std::make_unique<Connection>(Connection::Options{30000, 2000, 5000}, hooks);
        connection->setCheck([this](Connection::By by) { checks.push_back(by); });
        connection->addListener([this](Connection::State s, Connection::Reason r) {
            heard.push_back(std::string(Connection::stateName(s)) + "/" + Connection::reasonName(r));
        });
    }

    // Runs the newest timer (and returns its delay); the others were superseded or are run by the test.
    int runTimer() {
        REQUIRE_FALSE(timers.empty());
        auto [ms, fn] = std::move(timers.back());
        timers.clear();
        fn();
        return ms;
    }
    void runPosts() {
        auto list = std::move(posts);
        posts.clear();
        for (auto& fn : list) {
            fn();
        }
    }
};

api::ApiError offline() {
    return api::makeApiError(api::ApiErrorKind::Offline, "connection refused");
}

}  // namespace

TEST_SUITE("ui") {

TEST_CASE("connection: no network at start: offline at once, no request; the poll checks once the network is back") {
    Harness h;
    h.network = false;
    CHECK_FALSE(h.connection->beforeCheck(Connection::By::Start));
    CHECK(h.connection->offline());
    CHECK(h.connection->reason() == Connection::Reason::NoNetwork);
    CHECK(h.checks.empty());
    CHECK(h.events == std::vector<std::string>{"offline/no_network/start"});
    CHECK(h.heard == std::vector<std::string>{"offline/no_network"});
    // The poll: still no network, then it comes back.
    CHECK(h.runTimer() == 2000);
    CHECK(h.checks.empty());
    h.network = true;
    CHECK(h.runTimer() == 2000);
    REQUIRE(h.checks.size() == 1);
    CHECK(h.checks[0] == Connection::By::Network);
    CHECK(h.connection->checked(Connection::By::Network, nullptr, 40) == Connection::Verdict::Online);
    CHECK(h.connection->online());
    CHECK(h.heard == std::vector<std::string>{"offline/no_network", "online/none"});
    CHECK(h.timers.empty());  // online: no timers
}

TEST_CASE("connection: a check that can't reach the server goes offline; 502 too; 500 and Auth are the screen's error") {
    Harness h;
    REQUIRE(h.connection->beforeCheck(Connection::By::Start));
    CHECK(h.connection->checking());
    const api::ApiError away = offline();
    CHECK(h.connection->checked(Connection::By::Start, &away, 5000) == Connection::Verdict::Offline);
    CHECK(h.connection->reason() == Connection::Reason::CantReach);
    CHECK(h.events.back() == "offline/cant_reach/start");

    Harness proxy;
    REQUIRE(proxy.connection->beforeCheck(Connection::By::Start));
    const api::ApiError badGateway = api::fromHttpStatus(502, "Bad Gateway");
    CHECK(badGateway.httpStatus == 502);
    CHECK(proxy.connection->checked(Connection::By::Start, &badGateway, 10) == Connection::Verdict::Offline);
    for (int status : {503, 504}) {
        CHECK(Connection::isOfflineError(api::fromHttpStatus(status)));
    }

    Harness broken;
    REQUIRE(broken.connection->beforeCheck(Connection::By::Start));
    const api::ApiError serverError = api::fromHttpStatus(500, "oops");
    CHECK(broken.connection->checked(Connection::By::Start, &serverError, 10) == Connection::Verdict::Failed);
    CHECK(broken.connection->state() == Connection::State::Unknown);
    CHECK(broken.timers.empty());  // not offline: Retry is the user's
    const api::ApiError auth = api::makeApiError(api::ApiErrorKind::Auth);
    CHECK_FALSE(Connection::isOfflineError(auth));
    CHECK_FALSE(Connection::isOfflineError(api::makeApiError(api::ApiErrorKind::Timeout)));  // slow, not away
}

TEST_CASE("connection: offline, the 30 s timer, focus and waking check; the first that works goes online, once") {
    Harness h;
    REQUIRE(h.connection->beforeCheck(Connection::By::Start));
    const api::ApiError away = offline();
    h.connection->checked(Connection::By::Start, &away, 5000);
    // The timer.
    CHECK(h.runTimer() == 30000);
    REQUIRE(h.checks.size() == 1);
    CHECK(h.checks.back() == Connection::By::Timer);
    // A check under way: focus doesn't start another.
    h.connection->focusBack();
    CHECK(h.checks.size() == 1);
    h.connection->checked(Connection::By::Timer, &away, 5000);
    // Focus back, then waking.
    h.connection->focusBack();
    REQUIRE(h.checks.size() == 2);
    CHECK(h.checks.back() == Connection::By::Focus);
    h.connection->checked(Connection::By::Focus, &away, 5000);
    h.connection->woke();
    REQUIRE(h.checks.size() == 3);
    CHECK(h.checks.back() == Connection::By::Wake);
    CHECK(h.connection->checked(Connection::By::Wake, nullptr, 30) == Connection::Verdict::Online);
    CHECK(h.heard == std::vector<std::string>{"offline/cant_reach", "online/none"});
    // A timer of the offline time that fires late does nothing.
    h.connection->focusBack();  // online: no check
    CHECK(h.checks.size() == 3);
    // While offline with the network gone, the timer finds no network: no request, the poll instead.
    h.connection->suspect();
    h.runPosts();
    REQUIRE(h.checks.size() == 4);
    h.connection->checked(Connection::By::Suspect, &away, 5000);
    h.network = false;
    CHECK(h.runTimer() == 30000);
    CHECK(h.checks.size() == 4);
    CHECK(h.connection->reason() == Connection::Reason::NoNetwork);
    CHECK(h.runTimer() == 2000);  // the poll
}

TEST_CASE("connection: online, a failed request is a suspicion: one check per gap, only an offline result flips it") {
    Harness h;
    REQUIRE(h.connection->beforeCheck(Connection::By::Start));
    h.connection->checked(Connection::By::Start, nullptr, 20);
    h.heard.clear();
    // Many failed requests at once: one post, one check.
    h.connection->suspect();
    h.connection->suspect();
    CHECK(h.posts.size() == 1);
    h.runPosts();
    REQUIRE(h.checks.size() == 1);
    CHECK(h.checks[0] == Connection::By::Suspect);
    // It answered with an error that isn't "away": still online.
    const api::ApiError serverError = api::fromHttpStatus(500);
    CHECK(h.connection->checked(Connection::By::Suspect, &serverError, 20) == Connection::Verdict::Ignored);
    CHECK(h.connection->online());
    // Within the gap: no check; after it: one, and this time the server is away.
    h.now += 1000;
    h.connection->suspect();
    h.runPosts();
    CHECK(h.checks.size() == 1);
    h.now += 5000;
    h.connection->suspect();
    h.runPosts();
    REQUIRE(h.checks.size() == 2);
    const api::ApiError away = offline();
    CHECK(h.connection->checked(Connection::By::Suspect, &away, 5000) == Connection::Verdict::Offline);
    CHECK(h.heard == std::vector<std::string>{"offline/cant_reach"});
    // Offline: suspicions don't check (the timer does).
    h.now += 10000;
    h.connection->suspect();
    h.runPosts();
    CHECK(h.checks.size() == 2);
    // Waking while online checks too, and an error there is ignored.
    Harness w;
    REQUIRE(w.connection->beforeCheck(Connection::By::Start));
    w.connection->checked(Connection::By::Start, nullptr, 20);
    w.connection->woke();
    REQUIRE(w.checks.size() == 1);
    CHECK(w.checks[0] == Connection::By::Wake);
    CHECK(w.connection->checked(Connection::By::Wake, &serverError, 20) == Connection::Verdict::Ignored);
}

TEST_CASE("connection: the clock offset, reset, and timers after it is gone") {
    Harness h;
    CHECK(h.connection->clockOffset() == 0);
    CHECK_FALSE(h.connection->clockKnown());
    h.connection->noteServerDate(1795150998, 1795150990);
    CHECK(h.connection->clockOffset() == 8);
    CHECK(h.connection->clockKnown());
    h.connection->noteServerDate(1795150998, 1795151000);
    CHECK(h.connection->clockOffset() == -2);  // the newest answer wins

    REQUIRE(h.connection->beforeCheck(Connection::By::Start));
    const api::ApiError away = offline();
    h.connection->checked(Connection::By::Start, &away, 5000);
    REQUIRE_FALSE(h.timers.empty());
    h.connection->reset();  // another server saved
    CHECK(h.connection->state() == Connection::State::Unknown);
    h.runTimer();  // the old offline timer: nothing
    CHECK(h.checks.empty());

    // Gone with a timer and a post out.
    REQUIRE(h.connection->beforeCheck(Connection::By::Start));
    h.connection->checked(Connection::By::Start, &away, 5000);
    h.connection.reset();
    h.runTimer();
    h.runPosts();
    CHECK(h.checks.empty());
}

}  // TEST_SUITE
