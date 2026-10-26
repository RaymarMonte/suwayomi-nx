#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace snx::net {

template <typename T>
class Promise;

// A result that arrives later (D37). Unlike std::future it can run a callback when the result is there, which is how answers
// reach the UI: future.then([](const HttpResult& r) { brls::sync([r] { ... }); }).
// - then(fn): fn runs once, on the thread that completes the future (the network thread), or at once on the calling thread if
//   the result is already there. Keep it short (hand the result over and return); never block in it. One callback per future.
// - wait() / waitFor(): block until the result is there. For tests and tools, never on the UI thread.
// - get(): the result. Waits first if it isn't there yet.
template <typename T>
class Future {
  public:
    Future() = default;  // an empty future: valid() is false, don't use it

    bool valid() const { return state_ != nullptr; }

    bool ready() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->value.has_value();
    }

    void wait() const {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->ready.wait(lock, [this] { return state_->value.has_value(); });
    }

    // True if the result arrived within the time.
    template <typename Rep, typename Period>
    bool waitFor(std::chrono::duration<Rep, Period> timeout) const {
        std::unique_lock<std::mutex> lock(state_->mutex);
        return state_->ready.wait_for(lock, timeout, [this] { return state_->value.has_value(); });
    }

    // The value never changes once set, so the reference stays valid while any copy of this future lives.
    const T& get() const {
        wait();
        return *state_->value;
    }

    void then(std::function<void(const T&)> callback) {
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (!state_->value) {
            state_->callback = std::move(callback);
            return;
        }
        lock.unlock();
        callback(*state_->value);
    }

  private:
    friend class Promise<T>;

    struct State {
        std::mutex mutex;
        std::condition_variable ready;
        std::optional<T> value;
        std::function<void(const T&)> callback;
    };

    explicit Future(std::shared_ptr<State> state) : state_(std::move(state)) {}

    std::shared_ptr<State> state_;
};

// The other end of a Future: whoever does the work calls set() once.
template <typename T>
class Promise {
  public:
    Promise() : state_(std::make_shared<State>()) {}

    Future<T> future() const { return Future<T>(state_); }

    // Stores the result, wakes everyone waiting and runs the callback (on this thread). Only the first call counts.
    void set(T value) {
        std::function<void(const T&)> callback;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->value) {
                return;
            }
            state_->value.emplace(std::move(value));
            callback = std::move(state_->callback);
        }
        state_->ready.notify_all();
        if (callback) {
            callback(*state_->value);
        }
    }

  private:
    using State = typename Future<T>::State;
    std::shared_ptr<State> state_;
};

}  // namespace snx::net
