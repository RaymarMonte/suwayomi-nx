#include "image/WorkerPool.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace snx::image {

std::int64_t steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

WorkerPool::WorkerPool(int threads, std::uint64_t stackBytes) {
    // pthreads, not std::thread: only pthreads can be given a stack size (net does the same, D37).
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, static_cast<std::size_t>(stackBytes));
    for (int i = 0; i < threads; ++i) {
        pthread_t thread;
        if (pthread_create(&thread, &attributes, &WorkerPool::threadMain, this) != 0) {
            break;  // fewer threads than asked; threads() says how many
        }
        threads_.push_back(thread);
    }
    pthread_attr_destroy(&attributes);
}

WorkerPool::~WorkerPool() {
    stop();
}

void WorkerPool::post(int priority, std::function<void()> task) {
    postAfter(0, priority, std::move(task));
}

void WorkerPool::postAfter(int delayMs, int priority, std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        tasks_.push_back(Task{priority, steadyMs() + std::max(delayMs, 0), nextOrder_++, std::move(task)});
    }
    wake_.notify_one();
}

void WorkerPool::stop() {
    std::vector<Task> dropped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ && threads_.empty()) {
            return;
        }
        stopping_ = true;
        dropped.swap(tasks_);
    }
    wake_.notify_all();
    for (const pthread_t thread : threads_) {
        pthread_join(thread, nullptr);
    }
    threads_.clear();
    // `dropped` goes here, outside the lock: a task's captures may own things whose destructors take other locks.
}

std::size_t WorkerPool::waiting() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void* WorkerPool::threadMain(void* self) {
    static_cast<WorkerPool*>(self)->loop();
    return nullptr;
}

void WorkerPool::loop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
        // The most urgent task that is due. A linear look is fine: the loader keeps at most imageQueueLimit jobs.
        const std::int64_t now = steadyMs();
        auto best = tasks_.end();
        std::int64_t nextDue = -1;
        for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
            if (it->dueMs > now) {
                nextDue = nextDue < 0 ? it->dueMs : std::min(nextDue, it->dueMs);
                continue;
            }
            if (best == tasks_.end() || it->priority < best->priority ||
                (it->priority == best->priority && it->order < best->order)) {
                best = it;
            }
        }
        if (best == tasks_.end()) {
            if (nextDue < 0) {
                wake_.wait(lock);
            } else {
                wake_.wait_for(lock, std::chrono::milliseconds(nextDue - now));
            }
            continue;
        }
        std::function<void()> run = std::move(best->run);
        tasks_.erase(best);
        lock.unlock();
        try {
            run();
        } catch (const std::exception& e) {
            if (onException_) {
                onException_(e.what());
            }
        } catch (...) {
            if (onException_) {
                onException_("an exception of an unknown type");
            }
        }
        run = nullptr;  // its captures go before the lock is taken again
        lock.lock();
    }
}

}  // namespace snx::image
