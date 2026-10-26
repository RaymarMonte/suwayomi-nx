#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include <pthread.h>

// A few worker threads with an explicit stack size each (SAFETY.md S8: decoders 1 MiB, other workers 256 KiB), for the image
// module's decode and disk work (WP-07). Tasks run by priority (lower number first, net::Priority's order), then first come
// first served; a task can also wait a while before it may run (postAfter). An exception that escapes a task is caught at the
// task boundary (S1) and reported, the thread carries on.
namespace snx::image {

class WorkerPool {
  public:
    WorkerPool(int threads, std::uint64_t stackBytes);
    ~WorkerPool();  // stop()
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void post(int priority, std::function<void()> task);
    void postAfter(int delayMs, int priority, std::function<void()> task);

    // Drops the waiting tasks (they never run), waits for the running ones, ends the threads. Later posts are dropped too.
    // Never call it from a task.
    void stop();

    int threads() const { return static_cast<int>(threads_.size()); }  // how many could be started (0 = none at all)
    std::size_t waiting() const;

    // Gets the message of an exception that escaped a task. Set it before the first post.
    void setOnException(std::function<void(const char*)> handler) { onException_ = std::move(handler); }

  private:
    struct Task {
        int priority = 0;
        std::int64_t dueMs = 0;
        std::uint64_t order = 0;
        std::function<void()> run;
    };
    static void* threadMain(void* self);
    void loop();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<Task> tasks_;
    std::vector<pthread_t> threads_;
    std::uint64_t nextOrder_ = 0;
    bool stopping_ = false;
    std::function<void(const char*)> onException_;
};

// Milliseconds on a steady clock (for due times and the loader's timings).
std::int64_t steadyMs();

}  // namespace snx::image
