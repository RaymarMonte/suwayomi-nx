#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

// The test thread plays the app's main thread (WP-07): a loader or client hands its answers to deliver(), and runUntil() runs
// them on the test thread until a condition holds.
class MainQueue {
  public:
    std::function<void(std::function<void()>)> deliver() {
        return [this](std::function<void()> task) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                tasks_.push_back(std::move(task));
            }
            wake_.notify_all();
        };
    }

    // Runs what is delivered until `done()`; false after 10 s without it.
    bool runUntil(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done()) {
            std::vector<std::function<void()>> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!wake_.wait_until(lock, deadline, [this] { return !tasks_.empty(); })) {
                    return done();
                }
                batch.swap(tasks_);
            }
            for (auto& task : batch) {
                task();
            }
        }
        return true;
    }

    // Runs what is delivered until nothing more comes for `quietMs` (everything under way has ended).
    void settle(int quietMs = 200) {
        while (true) {
            std::vector<std::function<void()>> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!wake_.wait_for(lock, std::chrono::milliseconds(quietMs), [this] { return !tasks_.empty(); })) {
                    return;
                }
                batch.swap(tasks_);
            }
            for (auto& task : batch) {
                task();
            }
        }
    }

  private:
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::function<void()>> tasks_;
};
