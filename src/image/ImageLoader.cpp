#include "image/ImageLoader.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace snx::image {

namespace {

// How often the loader looks for loads whose callers have all cancelled, to stop their download or decode. Like net's own
// cancel check (~50 ms), this is a polling interval, not a limit.
constexpr int kSweepMs = 50;

int priorityNumber(net::Priority priority) {
    return static_cast<int>(priority);
}

ImageError fromNetError(const core::Error& error) {
    switch (error.kind) {
        case core::ErrorKind::Timeout: return imageError(ImageErrorKind::Timeout, error.message);
        case core::ErrorKind::Cancelled: return imageError(ImageErrorKind::Cancelled, error.message);
        case core::ErrorKind::TooLarge: return imageError(ImageErrorKind::TooLarge, error.message);
        default: return imageError(ImageErrorKind::Network, error.message);
    }
}

}  // namespace

// Lets the network thread's callbacks find the loader, or learn that it's gone (shutdown() clears the pointer).
struct ImageLoader::Lifeline {
    std::mutex mutex;
    ImageLoader* loader = nullptr;
};

LoaderOptions loaderOptionsFrom(const core::RuntimeProfile& profile) {
    LoaderOptions options;
    options.pageCacheBytes = profile.imageCacheBytes;
    options.coverCacheBytes = profile.coverCacheBytes;
    options.decode = decodeLimitsFrom(profile);
    options.maxDecodeScale = profile.maxDecodeScale;
    options.maxTextureSide = profile.maxTextureSide;
    options.decodeThreads = profile.decodeThreads;
    options.decodeStackBytes = profile.decodeThreadStackBytes;
    options.workerStackBytes = profile.workerThreadStackBytes;
    options.queueLimit = profile.imageQueueLimit;
    options.authRetryMs = profile.imageAuthRetryMs;
    options.maxImageBytes = profile.maxImageBytes;
    options.httpTimeoutMs = profile.httpImageTimeoutMs;
    options.pressureCheckMs = profile.pressureCheckMs;
    options.profile = profile;
    options.disk = diskCacheOptionsFrom(profile);
    return options;
}

ImageLoader::ImageLoader(net::HttpClient& http, LoaderOptions options)
    : http_(http),
      options_(std::move(options)),
      pages_(options_.pageCacheBytes),
      covers_(options_.coverCacheBytes),
      pressure_(options_.profile),
      lifeline_(std::make_shared<Lifeline>()),
      decodePool_(std::max(1, options_.decodeThreads), options_.decodeStackBytes),
      diskPool_(1, options_.workerStackBytes) {
    lifeline_->loader = this;
    if (options_.fs != nullptr) {
        disk_ = std::make_unique<DiskCache>(*options_.fs, options_.disk);
    }
    const auto onException = [this](const char* what) { log(std::string("image: a task threw: ") + what); };
    decodePool_.setOnException(onException);
    diskPool_.setOnException(onException);
    if (decodePool_.threads() == 0 || diskPool_.threads() == 0) {
        log("image: the worker threads couldn't start, so no image will load");
    }
    if (options_.memory) {
        scheduleMemoryCheck();
    }
}

ImageLoader::~ImageLoader() {
    shutdown();
}

void ImageLoader::setEventObserver(std::function<void(const core::testevents::ImageEvent&)> observer) {
    std::lock_guard<std::mutex> lock(mutex_);
    eventObserver_ = std::move(observer);
}

void ImageLoader::setPressureObserver(std::function<void(int, const char*)> observer) {
    std::lock_guard<std::mutex> lock(mutex_);
    pressureObserver_ = std::move(observer);
}

core::ImageSize ImageLoader::clampBox(core::ImageSize box) const {
    // A side the caller limits is capped at maxDecodeScale times the screen; a side of 0 stays unlimited (webtoon height).
    const int maxWidth = static_cast<int>(std::lround(options_.screen.width * options_.maxDecodeScale));
    const int maxHeight = static_cast<int>(std::lround(options_.screen.height * options_.maxDecodeScale));
    return {box.width > 0 ? std::min(box.width, std::max(maxWidth, 1)) : 0,
            box.height > 0 ? std::min(box.height, std::max(maxHeight, 1)) : 0};
}

void ImageLoader::load(LoadRequest request, net::CancelToken cancel, LoadCallback done) {
    Waiter waiter{clampBox(request.box), request.kind, cancel, std::move(done)};
    ImagePtr hit;
    std::optional<ImageError> refused;
    JobPtr start;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || cancel.cancelled()) {
            refused = imageError(ImageErrorKind::Cancelled, "cancelled");
        } else if ((hit = cacheFor(request.kind).get(cacheKey(request.url, waiter.box)))) {
            // answered below, outside the lock
        } else if (prefetchPaused_.load() && request.priority >= net::Priority::Prefetch) {
            refused = imageError(ImageErrorKind::Paused, "memory is low, prefetching is paused");
        } else {
            const auto found = jobs_.find(request.url);
            if (found != jobs_.end() && !found->second->cancel.cancelled()) {
                // The same URL is on its way already: wait for that one.
                Job& job = *found->second;
                job.priority = std::min(job.priority, request.priority);  // decodes in a more urgent place in line
                job.waiters.push_back(std::move(waiter));
                return;
            }
            if (jobs_.size() >= static_cast<std::size_t>(std::max(options_.queueLimit, 1))) {
                refused = imageError(ImageErrorKind::Busy, "too many images are loading");
            } else {
                start = std::make_shared<Job>();
                start->url = request.url;
                start->priority = request.priority;
                start->startMs = steadyMs();
                start->waiters.push_back(std::move(waiter));
                jobs_[request.url] = start;  // replaces a job whose callers have all cancelled; that one ends by itself
            }
        }
    }
    if (hit) {
        core::testevents::ImageEvent event;
        event.url = request.url;
        event.source = "memory";
        event.format = imageFormatName(hit->format());
        event.width = hit->size().width;
        event.height = hit->size().height;
        event.sourceWidth = hit->sourceSize().width;
        event.sourceHeight = hit->sourceSize().height;
        event.tiles = static_cast<int>(hit->tiles().size());
        event.bytes = hit->bytes();
        emit(event);
        deliverTo(waiter, hit);
        return;
    }
    if (refused) {
        deliverTo(waiter, *refused);
        return;
    }
    startJob(start);
}

ImagePtr ImageLoader::cached(const std::string& url, core::ImageSize box, ImageKind kind) {
    return cacheFor(kind).get(cacheKey(url, clampBox(box)));
}

void ImageLoader::clearMemory() {
    pages_.clear();
    covers_.clear();
}

std::size_t ImageLoader::activeJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

void ImageLoader::deliverTo(const Waiter& waiter, const ImageResult& result) {
    if (!waiter.done) {
        return;
    }
    auto task = [done = waiter.done, cancel = waiter.cancel, result] {
        // Cancelled by now: the caller gets Cancelled, never a late image (its screen may be gone).
        if (cancel.cancelled() && (result.ok() || result.error().kind != ImageErrorKind::Cancelled)) {
            done(ImageResult(imageError(ImageErrorKind::Cancelled, "cancelled")));
        } else {
            done(result);
        }
    };
    if (options_.deliver) {
        options_.deliver(std::move(task));
    } else {
        task();
    }
}

bool ImageLoader::allCancelled(const JobPtr& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool all = std::all_of(job->waiters.begin(), job->waiters.end(),
                                 [](const Waiter& waiter) { return waiter.cancel.cancelled(); });
    if (all) {
        job->cancel.cancel();
    }
    return all || stopped_;
}

void ImageLoader::startJob(const JobPtr& job) {
    scheduleSweep();
    if (disk_) {
        diskPool_.post(priorityNumber(job->priority), [this, job] { readDisk(job); });
    } else {
        fetch(job);
    }
}

void ImageLoader::readDisk(const JobPtr& job) {
    if (allCancelled(job)) {
        fail(job, imageError(ImageErrorKind::Cancelled, "cancelled"));
        return;
    }
    auto bytes = disk_->read(job->url);
    if (!bytes) {
        fetch(job);
        return;
    }
    job->source = "disk";
    job->fetchMs = steadyMs() - job->startMs;
    auto shared = std::make_shared<const std::string>(std::move(*bytes));
    decodePool_.post(priorityNumber(job->priority), [this, job, shared] { decodeJob(job, shared); });
}

void ImageLoader::fetch(const JobPtr& job) {
    net::Request request;
    request.url = job->url;
    request.maxBodyBytes = options_.maxImageBytes;  // stopped while it streams in if it's bigger (S6)
    request.timeoutMs = options_.httpTimeoutMs;
    // The callback runs on the network thread. The lifeline keeps it from touching a loader that has shut down.
    http_.submit(std::move(request), job->priority, job->cancel)
        .then([life = lifeline_, job](const net::HttpResult& result) {
            std::lock_guard<std::mutex> lock(life->mutex);
            if (life->loader != nullptr) {
                life->loader->onFetched(job, result);
            }
        });
}

void ImageLoader::onFetched(const JobPtr& job, const net::HttpResult& result) {
    if (!result.ok()) {
        fail(job, fromNetError(result.error()));
        return;
    }
    const net::Response& response = result.value();
    if (response.status == 401 && !job->authRetried) {
        // ui_login: the api layer saw this 401 too and is renewing the token (D38). Try once more in a moment.
        job->authRetried = true;
        diskPool_.postAfter(options_.authRetryMs, priorityNumber(job->priority), [this, job] {
            if (allCancelled(job)) {
                fail(job, imageError(ImageErrorKind::Cancelled, "cancelled"));
            } else {
                fetch(job);
            }
        });
        return;
    }
    if (response.status < 200 || response.status > 299) {
        fail(job, imageError(ImageErrorKind::Http, "the server answered HTTP " + std::to_string(response.status),
                             response.status));
        return;
    }
    if (response.body.empty()) {
        fail(job, imageError(ImageErrorKind::Corrupt, "the server sent an empty file"));
        return;
    }
    job->source = "net";
    job->fetchMs = steadyMs() - job->startMs;
    auto bytes = std::make_shared<const std::string>(response.body);
    decodePool_.post(priorityNumber(job->priority), [this, job, bytes] { decodeJob(job, bytes); });
}

void ImageLoader::decodeJob(const JobPtr& job, std::shared_ptr<const std::string> bytes) {
    // The boxes (and caches) the waiting callers want; usually just one.
    std::vector<std::pair<core::ImageSize, ImageKind>> wanted;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Waiter& waiter : job->waiters) {
            const bool seen = std::any_of(wanted.begin(), wanted.end(), [&](const auto& w) {
                return w.first == waiter.box && w.second == waiter.kind;
            });
            if (!waiter.cancel.cancelled() && !seen) {
                wanted.emplace_back(waiter.box, waiter.kind);
            }
        }
    }
    if (wanted.empty() || allCancelled(job)) {
        fail(job, imageError(ImageErrorKind::Cancelled, "cancelled"));
        return;
    }
    std::map<std::string, ImageResult> results;  // by cacheKey(url, box)
    core::testevents::ImageEvent event;
    event.url = job->url;
    event.source = job->source;
    event.fetchMs = static_cast<int>(job->fetchMs);
    event.fileBytes = bytes->size();
    const MemoryProbe probe = options_.memory ? MemoryProbe([this] {
        const auto sample = options_.memory();
        return sample ? sample->used : 0;
    })
                                              : MemoryProbe();
    bool anyDecoded = false;
    for (const auto& [box, kind] : wanted) {
        const std::string key = cacheKey(job->url, box);
        auto existing = results.find(key);
        if (existing == results.end()) {
            const std::int64_t started = steadyMs();
            DecodeInfo info;
            auto decoded = decodeImage(*bytes, box, options_.decode, job->cancel, &info, probe);
            event.format = imageFormatName(info.header.format);
            event.sourceWidth = info.header.size.width;
            event.sourceHeight = info.header.size.height;
            event.memUsed = info.memUsedAtPeak;
            if (decoded.ok()) {
                auto image = std::make_shared<const DecodedImage>(std::move(decoded.value()), options_.maxTextureSide,
                                                                  info.header.format, info.header.size);
                event.width = image->size().width;
                event.height = image->size().height;
                event.tiles = static_cast<int>(image->tiles().size());
                event.bytes = image->bytes();
                event.error.clear();
                anyDecoded = true;
                existing = results.emplace(key, ImageResult(ImagePtr(image))).first;
            } else {
                event.error = imageErrorKindName(decoded.error().kind);
                existing = results.emplace(key, ImageResult(decoded.error())).first;
            }
            event.decodeMs = static_cast<int>(steadyMs() - started);
        }
        if (existing->second.ok()) {
            cacheFor(kind).put(key, existing->second.value());
        }
    }
    finish(job, results, nullptr, event);
    if (anyDecoded && job->source == "net" && disk_) {
        // Only images that decoded go to the disk cache: a broken file is fetched again next time (the server may fix it).
        diskPool_.post(priorityNumber(net::Priority::Background), [this, url = job->url, bytes] {
            const DiskCache::Write written = disk_->write(url, *bytes);
            if (written == DiskCache::Write::NoSpace || written == DiskCache::Write::Failed) {
                log(std::string("image: not cached on disk (") +
                    (written == DiskCache::Write::NoSpace ? "too little free space" : "write failed") + "): " + url);
            }
        });
    }
}

void ImageLoader::fail(const JobPtr& job, const ImageError& error) {
    core::testevents::ImageEvent event;
    event.url = job->url;
    event.source = job->source;
    event.error = imageErrorKindName(error.kind);
    event.fetchMs = static_cast<int>(job->fetchMs);
    finish(job, {}, &error, event);
}

void ImageLoader::finish(const JobPtr& job, const std::map<std::string, ImageResult>& results, const ImageError* error,
                         const core::testevents::ImageEvent& event) {
    std::vector<Waiter> waiters;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = jobs_.find(job->url);
        if (found != jobs_.end() && found->second == job) {
            jobs_.erase(found);
        }
        waiters.swap(job->waiters);
    }
    emit(event);
    for (Waiter& waiter : waiters) {
        if (error != nullptr) {
            deliverTo(waiter, *error);
            continue;
        }
        const auto result = results.find(cacheKey(job->url, waiter.box));
        if (result != results.end()) {
            deliverTo(waiter, result->second);
        } else {
            // Joined while the decode ran, with another box: load again for it (the bytes are probably on disk now).
            load(LoadRequest{job->url, waiter.box, job->priority, waiter.kind}, waiter.cancel, std::move(waiter.done));
        }
    }
}

void ImageLoader::scheduleSweep() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sweepScheduled_ || stopped_) {
            return;
        }
        sweepScheduled_ = true;
    }
    diskPool_.postAfter(kSweepMs, priorityNumber(net::Priority::Interactive), [this] { sweepCancelled(); });
}

void ImageLoader::sweepCancelled() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sweepScheduled_ = false;
        for (const auto& entry : jobs_) {
            Job& job = *entry.second;
            const bool all = std::all_of(job.waiters.begin(), job.waiters.end(),
                                         [](const Waiter& waiter) { return waiter.cancel.cancelled(); });
            if (all) {
                job.cancel.cancel();  // net drops its download within ~50 ms; a decode stops at its next check
            }
        }
        if (jobs_.empty()) {
            return;
        }
    }
    scheduleSweep();
}

void ImageLoader::scheduleMemoryCheck() {
    diskPool_.postAfter(std::max(options_.pressureCheckMs, 1), priorityNumber(net::Priority::Background), [this] {
        checkMemoryNow();
        scheduleMemoryCheck();
    });
}

void ImageLoader::checkMemoryNow() {
    if (!options_.memory) {
        return;
    }
    const auto sample = options_.memory();
    if (!sample) {
        return;
    }
    core::MemoryPressure::Change change = core::MemoryPressure::Change::None;
    int usedPercent = 0;
    int shrinkPercent = 100;
    std::function<void(int, const char*)> observer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        change = pressure_.update(*sample);
        usedPercent = pressure_.usedPercent();
        shrinkPercent = pressure_.shrinkPercent();
        observer = pressureObserver_;
    }
    if (change == core::MemoryPressure::Change::None) {
        return;
    }
    const bool entered = change == core::MemoryPressure::Change::Entered;
    prefetchPaused_.store(entered);
    const std::uint64_t pages = entered ? options_.pageCacheBytes / 100 * static_cast<std::uint64_t>(shrinkPercent)
                                        : options_.pageCacheBytes;
    const std::uint64_t covers = entered ? options_.coverCacheBytes / 100 * static_cast<std::uint64_t>(shrinkPercent)
                                         : options_.coverCacheBytes;
    pages_.setBudget(pages);
    covers_.setBudget(covers);
    const char* action = entered ? "shrink_caches" : "recovered";
    log(std::string("image: memory ") + std::to_string(usedPercent) + "% in use: " + action);
    if (observer) {
        observer(usedPercent, action);
    }
}

void ImageLoader::emit(const core::testevents::ImageEvent& event) {
    std::function<void(const core::testevents::ImageEvent&)> observer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        observer = eventObserver_;
    }
    if (observer) {
        observer(event);
    }
}

void ImageLoader::log(const std::string& line) const {
    if (options_.log) {
        options_.log(line);
    }
}

void ImageLoader::shutdown() {
    {
        // From now on the network thread's callbacks do nothing (and one that is running finishes first).
        std::lock_guard<std::mutex> lock(lifeline_->mutex);
        lifeline_->loader = nullptr;
    }
    std::map<std::string, JobPtr> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        jobs.swap(jobs_);
    }
    for (const auto& entry : jobs) {
        entry.second->cancel.cancel();
    }
    if (disk_) {
        disk_->stop();  // S5: no new cache writes; one that is being written finishes below
    }
    decodePool_.stop();
    diskPool_.stop();
    // Whatever was still waiting gets its answer: Cancelled.
    for (const auto& entry : jobs) {
        std::vector<Waiter> waiters;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            waiters.swap(entry.second->waiters);
        }
        for (const Waiter& waiter : waiters) {
            deliverTo(waiter, imageError(ImageErrorKind::Cancelled, "the image loader has stopped"));
        }
    }
}

}  // namespace snx::image
