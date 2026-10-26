#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/ImageGeometry.hpp"
#include "core/MemoryPressure.hpp"
#include "core/RuntimeProfile.hpp"
#include "core/TestEvents.hpp"
#include "image/Caches.hpp"
#include "image/Decode.hpp"
#include "image/DecodedImage.hpp"
#include "image/DiskCache.hpp"
#include "image/ImageError.hpp"
#include "image/WorkerPool.hpp"
#include "net/CancelToken.hpp"
#include "net/HttpClient.hpp"

// Loads images for the screens (WP-07, D39): memory cache → disk cache → download → decode → tiles, then the answer on the
// main thread. One loader for the whole app, made in main() after the HttpClient (the one SuwayomiClient decorates, so
// requests to the server carry the login).
namespace snx::image {

enum class ImageKind {
    Page,   // reader pages: the big memory cache (imageCacheBytes)
    Cover,  // thumbnails: their own smaller cache (coverCacheBytes), so a library grid never pushes out reader pages
};

struct LoadRequest {
    std::string url;     // full URL (SuwayomiClient::absoluteUrl)
    core::ImageSize box; // fit inside this; a side of 0 = no limit that way (a webtoon strip: {width, 0})
    net::Priority priority = net::Priority::Interactive;
    ImageKind kind = ImageKind::Page;
};

using ImageResult = ImageResultOf<ImagePtr>;
using LoadCallback = std::function<void(const ImageResult&)>;

struct LoaderOptions {
    // Every limit, from the RuntimeProfile (loaderOptionsFrom): never constants of our own.
    std::uint64_t pageCacheBytes = 0;
    std::uint64_t coverCacheBytes = 0;
    DecodeLimits decode;
    float maxDecodeScale = 2.0f;
    int maxTextureSide = 2048;
    int decodeThreads = 2;
    std::uint64_t decodeStackBytes = 1024 * 1024;
    std::uint64_t workerStackBytes = 256 * 1024;
    int queueLimit = 128;
    int authRetryMs = 1000;
    std::uint64_t maxImageBytes = 0;
    int httpTimeoutMs = 0;
    int pressureCheckMs = 2000;
    core::RuntimeProfile profile;  // for the memory-pressure guard's percentages

    // The screen, so a box can't ask for more than maxDecodeScale times it (ARCHITECTURE.md "Max decode resolution").
    core::ImageSize screen{1280, 720};

    // Hands a finished load to the main thread: the app passes brls::sync. Tests: a queue they run themselves.
    std::function<void(std::function<void()>)> deliver;
    // The disk cache: the app's SafeFs (must outlive the loader), or nullptr for none.
    const platform::SafeFs* fs = nullptr;
    DiskCacheOptions disk;
    // The allocator's in-use bytes and the total (platform::runtimeInfo()); empty = no memory-pressure guard, no memUsed.
    std::function<std::optional<core::MemorySample>()> memory;
    // Short log lines (the app passes its logger). May be empty.
    std::function<void(const std::string&)> log;
};
LoaderOptions loaderOptionsFrom(const core::RuntimeProfile& profile);

class ImageLoader {
  public:
    ImageLoader(net::HttpClient& http, LoaderOptions options);
    ~ImageLoader();  // shutdown()
    ImageLoader(const ImageLoader&) = delete;
    ImageLoader& operator=(const ImageLoader&) = delete;

    // Starts a load; `done` runs once, on the main thread (options.deliver), with the image or an ImageError. Any thread.
    // A load cancelled before its answer is delivered ends Cancelled: check that first and return (the screen may be gone).
    // Loads of the same URL at the same time share one download (and one decode per box). Prefetch and Background loads are
    // refused (Paused) while memory is low.
    void load(LoadRequest request, net::CancelToken cancel, LoadCallback done);

    // The decoded image if it's in the memory cache right now, else nullptr. For a screen that wants to draw at once.
    ImagePtr cached(const std::string& url, core::ImageSize box, ImageKind kind);

    // Drops the decoded images (not the disk cache).
    void clearMemory();

    // Ends every load (Cancelled), stops the threads, stops disk writes (S5). Later loads end Cancelled at once.
    // The destructor calls it. Never from a `done` callback that runs on a loader thread (they don't: they run on main).
    void shutdown();

    // Called with every finished load, on a loader thread (keep it short): the test hooks' "image" events.
    void setEventObserver(std::function<void(const core::testevents::ImageEvent&)> observer);
    // Called when the memory-pressure guard changes state, with the used percent and "shrink_caches" / "recovered".
    void setPressureObserver(std::function<void(int usedPercent, const char* action)> observer);

    // For tests and the idle check.
    std::size_t activeJobs() const;
    bool prefetchPaused() const { return prefetchPaused_.load(); }
    std::uint64_t pageCacheBytes() const { return pages_.bytes(); }
    std::uint64_t coverCacheBytes() const { return covers_.bytes(); }
    // One memory-pressure check now (normally every pressureCheckMs on the disk thread).
    void checkMemoryNow();
    DiskCache* diskCache() { return disk_.get(); }

  private:
    struct Waiter {
        core::ImageSize box;
        ImageKind kind;
        net::CancelToken cancel;
        LoadCallback done;
    };
    struct Job {
        std::string url;
        net::Priority priority;
        net::CancelToken cancel;  // the job's own: fired when every waiter has cancelled
        std::vector<Waiter> waiters;
        bool authRetried = false;
        std::string source;  // "disk" or "net"
        std::int64_t startMs = 0;
        std::int64_t fetchMs = 0;
    };
    using JobPtr = std::shared_ptr<Job>;
    struct Lifeline;

    core::ImageSize clampBox(core::ImageSize box) const;
    bool allCancelled(const JobPtr& job);  // true (and the job's token fired) once every caller has cancelled
    MemoryCache& cacheFor(ImageKind kind) { return kind == ImageKind::Cover ? covers_ : pages_; }
    void deliverTo(const Waiter& waiter, const ImageResult& result);
    void startJob(const JobPtr& job);
    void readDisk(const JobPtr& job);
    void fetch(const JobPtr& job);
    void onFetched(const JobPtr& job, const net::HttpResult& result);
    void decodeJob(const JobPtr& job, std::shared_ptr<const std::string> bytes);
    void finish(const JobPtr& job, const std::map<std::string, ImageResult>& results, const ImageError* error,
                const core::testevents::ImageEvent& event);
    void fail(const JobPtr& job, const ImageError& error);
    void sweepCancelled();
    void scheduleSweep();
    void scheduleMemoryCheck();
    void emit(const core::testevents::ImageEvent& event);
    void log(const std::string& line) const;

    net::HttpClient& http_;
    LoaderOptions options_;
    MemoryCache pages_;
    MemoryCache covers_;
    std::unique_ptr<DiskCache> disk_;
    core::MemoryPressure pressure_;  // under mutex_
    std::shared_ptr<Lifeline> lifeline_;
    std::atomic<bool> prefetchPaused_{false};

    mutable std::mutex mutex_;
    std::map<std::string, JobPtr> jobs_;  // by URL
    bool stopped_ = false;
    bool sweepScheduled_ = false;
    std::function<void(const core::testevents::ImageEvent&)> eventObserver_;
    std::function<void(int, const char*)> pressureObserver_;

    // Last, so they stop (in the destructor) before anything they use goes away.
    WorkerPool decodePool_;
    WorkerPool diskPool_;  // disk reads/writes, timers (cancel sweep, 401 retry, memory checks)
};

}  // namespace snx::image
