#include "net/CurlHttpClient.hpp"

#include <curl/curl.h>
#include <pthread.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "core/Version.hpp"

namespace snx::net {

namespace {

using Clock = std::chrono::steady_clock;

// While transfers run, the network thread wakes at least this often, so a cancelled token stops its transfer quickly.
constexpr int kPollMs = 50;

std::once_flag curlInitOnce;

// One request from submit() until its future completes. A retry puts it back in the queue with a later notBefore.
struct Job {
    Request request;  // as submitted; decorated again before each attempt
    Priority priority = Priority::Background;
    CancelToken cancel;
    Promise<HttpResult> promise;
    int attempts = 0;
    Clock::time_point notBefore{};
};

// One running attempt of a job, owned by the network thread.
struct Transfer {
    Job job;
    Request sent;  // this attempt's request, after the RequestDecorator
    CURL* easy = nullptr;
    curl_slist* headerList = nullptr;
    Response response;
    std::uint64_t maxBody = 0;
    std::uint64_t maxHeaders = 0;
    std::uint64_t headerBytes = 0;
    bool tooLarge = false;
    char errorText[CURL_ERROR_SIZE] = {};
};

int queueIndex(Priority priority) {
    int index = static_cast<int>(priority);
    return index >= 0 && index < kPriorityCount ? index : kPriorityCount - 1;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string lowerCase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// curl hands over the answer's header lines one by one. Returning anything but `size * count` stops the transfer.
size_t onHeader(char* data, size_t size, size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const size_t n = size * count;
    t->headerBytes += n;
    if (t->headerBytes > t->maxHeaders) {
        t->tooLarge = true;
        return 0;
    }
    std::string_view line(data, n);
    if (line.substr(0, 5) == "HTTP/") {
        t->response.headers.clear();  // a new answer starts (after a redirect): keep only the last one's headers
        return n;
    }
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return n;
    }
    std::string name = lowerCase(trim(line.substr(0, colon)));
    std::string value(trim(line.substr(colon + 1)));
    if (name == "content-length") {
        // An answer that announces a body over the limit stops before any of it arrives.
        const unsigned long long length = std::strtoull(value.c_str(), nullptr, 10);
        if (length > t->maxBody) {
            t->tooLarge = true;
            return 0;
        }
        t->response.body.reserve(static_cast<size_t>(length));
    }
    t->response.headers.emplace_back(std::move(name), std::move(value));
    return n;
}

// curl hands over the (un-gzipped) body piece by piece. The limit is checked before each piece is kept (SAFETY.md S6).
size_t onBody(char* data, size_t size, size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const size_t n = size * count;
    if (t->response.body.size() + n > t->maxBody) {
        t->tooLarge = true;
        return 0;
    }
    t->response.body.append(data, n);
    return n;
}

// Problems that may be gone a moment later, so a GET is worth trying again.
bool transientCurlError(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
            return true;
        default:
            return false;
    }
}

std::string describe(CURLcode code, const Transfer& t) {
    std::string text = curl_easy_strerror(code);
    if (t.errorText[0] != '\0') {
        text += ": ";
        text += t.errorText;
    }
    return text;
}

HttpResult cancelledResult(const char* why) {
    return HttpResult::fail(core::ErrorKind::Cancelled, why);
}

}  // namespace

CurlOptions curlOptionsFrom(const core::RuntimeProfile& profile) {
    CurlOptions options;
    options.maxConcurrent = profile.httpConcurrency;
    options.defaultMaxBodyBytes = profile.json.maxBytes;
    options.maxHeaderBytes = profile.httpMaxHeaderBytes;
    options.connectTimeoutMs = profile.httpConnectTimeoutMs;
    options.timeoutMs = profile.httpTimeoutMs;
    options.maxRedirects = profile.httpMaxRedirects;
    options.retry = profile.httpRetry;
    options.threadStackBytes = profile.netThreadStackBytes;
    options.userAgent = "suwayomi-nx/" + std::string(core::appVersion());
    return options;
}

struct CurlHttpClient::Impl {
    Impl(CurlHttpClient& owner, CurlOptions options);
    ~Impl();

    CurlHttpClient& owner;  // for its hooks (decorate, notifyObservers)
    const CurlOptions options;

    // Shared with the threads that submit; guarded by `mutex`.
    std::mutex mutex;
    std::condition_variable wake;  // the network thread sleeps here while it has nothing to do
    std::array<std::deque<Job>, kPriorityCount> queues;
    bool stopping = false;
    bool resetRequested = false;
    CURLM* multi = nullptr;  // only the network thread replaces it; others only call curl_multi_wakeup on it

    bool threadStarted = false;  // set in the constructor, read-only afterwards
    pthread_t thread{};
    std::mutex joinMutex;
    bool joined = false;

    // Accepted requests that haven't finished yet (inFlight()): up at submit, down in finish().
    std::atomic<std::size_t> inFlight{0};

    // The network thread's own.
    std::vector<std::unique_ptr<Transfer>> active;

    static void* threadMain(void* self);
    void run();
    void wakeLocked();
    CURLM* newMulti() const;
    std::optional<Job> popReadyLocked(Clock::time_point now);
    std::optional<Clock::time_point> nextRetryLocked() const;
    void takeCancelledLocked(std::vector<Job>& out);
    void start(Job job);
    void pump();
    void collectFinished();
    void dropCancelledTransfers();
    void resetNow();
    void stopAll();
    std::unique_ptr<Transfer> detach(Transfer* transfer);
    void conclude(std::unique_ptr<Transfer> transfer, CURLcode code, long status);
    void retryOrFinish(Job job, const Request& sent, HttpResult result, bool transient);
    void finish(Job& job, const Request& sent, HttpResult result);
};

CurlHttpClient::Impl::Impl(CurlHttpClient& owner_, CurlOptions options_) : owner(owner_), options(std::move(options_)) {
    std::call_once(curlInitOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    multi = newMulti();
    // An explicit stack size (WP-05), so the thread has the same room on the Switch and on the PC.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (options.threadStackBytes > 0) {
        pthread_attr_setstacksize(&attr, static_cast<size_t>(options.threadStackBytes));
    }
    threadStarted = multi != nullptr && pthread_create(&thread, &attr, &Impl::threadMain, this) == 0;
    pthread_attr_destroy(&attr);
    if (!threadStarted) {
        stopping = true;  // every submit fails at once
    }
}

CurlHttpClient::Impl::~Impl() {
    if (multi != nullptr) {
        curl_multi_cleanup(multi);
    }
}

void* CurlHttpClient::Impl::threadMain(void* self) {
    static_cast<Impl*>(self)->run();
    return nullptr;
}

void CurlHttpClient::Impl::wakeLocked() {
    wake.notify_one();
    if (multi != nullptr) {
        curl_multi_wakeup(multi);  // ends a curl_multi_poll early
    }
}

CURLM* CurlHttpClient::Impl::newMulti() const {
    CURLM* m = curl_multi_init();
    if (m != nullptr) {
        // Never more sockets than transfers: on the Switch every socket counts against the BSD service's limits.
        const long limit = std::max(1, options.maxConcurrent);
        curl_multi_setopt(m, CURLMOPT_MAX_TOTAL_CONNECTIONS, limit);
        curl_multi_setopt(m, CURLMOPT_MAXCONNECTS, limit);
    }
    return m;
}

// The network thread's loop: drop cancelled jobs, start ready jobs up to maxConcurrent (most urgent first), let curl work.
void CurlHttpClient::Impl::run() {
    std::unique_lock<std::mutex> lock(mutex);
    while (!stopping) {
        if (resetRequested) {
            resetRequested = false;
            lock.unlock();
            resetNow();
            lock.lock();
            continue;
        }
        std::vector<Job> cancelled;
        takeCancelledLocked(cancelled);
        std::vector<Job> starting;
        const auto now = Clock::now();
        const size_t limit = static_cast<size_t>(std::max(1, options.maxConcurrent));
        while (active.size() + starting.size() < limit) {
            std::optional<Job> job = popReadyLocked(now);
            if (!job) {
                break;
            }
            starting.push_back(std::move(*job));
        }
        if (active.empty() && starting.empty() && cancelled.empty()) {
            // Nothing to do: sleep until a submit, or until a waiting retry is due (checking tokens meanwhile).
            if (std::optional<Clock::time_point> next = nextRetryLocked()) {
                wake.wait_until(lock, std::min(*next, now + std::chrono::milliseconds(kPollMs)));
            } else {
                wake.wait(lock);
            }
            continue;
        }
        lock.unlock();
        for (Job& job : cancelled) {
            finish(job, job.request, cancelledResult("cancelled"));
        }
        for (Job& job : starting) {
            start(std::move(job));
        }
        pump();
        lock.lock();
    }
    lock.unlock();
    stopAll();
}

std::optional<Job> CurlHttpClient::Impl::popReadyLocked(Clock::time_point now) {
    for (auto& queue : queues) {
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->notBefore <= now) {
                Job job = std::move(*it);
                queue.erase(it);
                return job;
            }
        }
    }
    return std::nullopt;
}

std::optional<Clock::time_point> CurlHttpClient::Impl::nextRetryLocked() const {
    std::optional<Clock::time_point> next;
    for (const auto& queue : queues) {
        for (const Job& job : queue) {
            if (!next || job.notBefore < *next) {
                next = job.notBefore;
            }
        }
    }
    return next;
}

void CurlHttpClient::Impl::takeCancelledLocked(std::vector<Job>& out) {
    for (auto& queue : queues) {
        for (auto it = queue.begin(); it != queue.end();) {
            if (it->cancel.cancelled()) {
                out.push_back(std::move(*it));
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void CurlHttpClient::Impl::start(Job job) {
    job.attempts += 1;
    auto t = std::make_unique<Transfer>();
    t->job = std::move(job);
    t->sent = t->job.request;
    owner.decorate(t->sent);
    t->maxBody = t->sent.maxBodyBytes > 0 ? t->sent.maxBodyBytes : options.defaultMaxBodyBytes;
    t->maxHeaders = options.maxHeaderBytes;

    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
        finish(t->job, t->sent, HttpResult::fail(core::ErrorKind::Network, "couldn't start a transfer (out of memory?)"));
        return;
    }
    t->easy = easy;
    const Request& r = t->sent;
    void* self = static_cast<void*>(t.get());
    curl_easy_setopt(easy, CURLOPT_URL, r.url.c_str());
    curl_easy_setopt(easy, CURLOPT_PRIVATE, self);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, t->errorText);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &onHeader);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, self);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &onBody);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, self);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(r.connectTimeoutMs > 0 ? r.connectTimeoutMs
                                                                                                : options.connectTimeoutMs));
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(r.timeoutMs > 0 ? r.timeoutMs : options.timeoutMs));
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, static_cast<long>(options.maxRedirects));
#if LIBCURL_VERSION_NUM >= 0x075500  // curl 7.85+ (the PC) takes the protocol list as text; the Switch has 7.69
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");  // every encoding curl can undo (gzip, deflate)
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, options.userAgent.c_str());
    if (r.method == Method::Post) {
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, r.body.c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(r.body.size()));
        // No "Expect: 100-continue" wait before bigger bodies.
        if (curl_slist* list = curl_slist_append(t->headerList, "Expect:")) {
            t->headerList = list;
        }
    }
    for (const auto& [name, value] : r.headers) {
        if (name.find_first_of("\r\n:") != std::string::npos || value.find_first_of("\r\n") != std::string::npos) {
            continue;  // a line break would smuggle in another header
        }
        if (curl_slist* list = curl_slist_append(t->headerList, (name + ": " + value).c_str())) {
            t->headerList = list;
        }
    }
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, t->headerList);

    if (curl_multi_add_handle(multi, easy) != CURLM_OK) {
        curl_slist_free_all(t->headerList);
        curl_easy_cleanup(easy);
        finish(t->job, t->sent, HttpResult::fail(core::ErrorKind::Network, "couldn't start a transfer"));
        return;
    }
    active.push_back(std::move(t));
}

void CurlHttpClient::Impl::pump() {
    int running = 0;
    curl_multi_perform(multi, &running);
    collectFinished();
    dropCancelledTransfers();
    if (!active.empty()) {
        curl_multi_poll(multi, nullptr, 0, kPollMs, nullptr);
    }
}

void CurlHttpClient::Impl::collectFinished() {
    int left = 0;
    while (CURLMsg* message = curl_multi_info_read(multi, &left)) {
        if (message->msg != CURLMSG_DONE) {
            continue;
        }
        CURL* easy = message->easy_handle;
        const CURLcode code = message->data.result;
        char* priv = nullptr;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &priv);
        long status = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
        conclude(detach(reinterpret_cast<Transfer*>(priv)), code, status);
    }
}

void CurlHttpClient::Impl::dropCancelledTransfers() {
    for (size_t i = 0; i < active.size();) {
        if (active[i]->job.cancel.cancelled()) {
            std::unique_ptr<Transfer> t = detach(active[i].get());
            finish(t->job, t->sent, cancelledResult("cancelled"));
        } else {
            ++i;
        }
    }
}

// Takes a running transfer out of curl and out of `active`, and frees its curl parts.
std::unique_ptr<Transfer> CurlHttpClient::Impl::detach(Transfer* transfer) {
    auto it = std::find_if(active.begin(), active.end(), [transfer](const auto& t) { return t.get() == transfer; });
    std::unique_ptr<Transfer> t = std::move(*it);
    active.erase(it);
    curl_multi_remove_handle(multi, t->easy);
    curl_easy_cleanup(t->easy);
    curl_slist_free_all(t->headerList);
    t->easy = nullptr;
    t->headerList = nullptr;
    return t;
}

void CurlHttpClient::Impl::conclude(std::unique_ptr<Transfer> t, CURLcode code, long status) {
    bool transient = false;
    std::optional<HttpResult> result;
    if (t->job.cancel.cancelled()) {
        result = cancelledResult("cancelled");
    } else if (code == CURLE_OK) {
        t->response.status = static_cast<int>(status);
        t->response.attempts = t->job.attempts;
        transient = core::isTransientHttpStatus(t->response.status);
        result = HttpResult(std::move(t->response));
    } else if (t->tooLarge) {
        result = HttpResult::fail(core::ErrorKind::TooLarge, "the server's answer is larger than this request allows (" +
                                                                 std::to_string(t->maxBody) + " bytes)");
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        result = HttpResult::fail(core::ErrorKind::Timeout, describe(code, *t));
    } else {
        transient = transientCurlError(code);
        result = HttpResult::fail(core::ErrorKind::Network, describe(code, *t));
    }
    retryOrFinish(std::move(t->job), t->sent, std::move(*result), transient);
}

void CurlHttpClient::Impl::retryOrFinish(Job job, const Request& sent, HttpResult result, bool transient) {
    const bool idempotent = job.request.method == Method::Get || job.request.idempotent;
    if (core::shouldRetry(options.retry, job.attempts - 1, idempotent, transient)) {
        job.notBefore = Clock::now() + std::chrono::milliseconds(core::retryDelayMs(options.retry, job.attempts));
        std::lock_guard<std::mutex> lock(mutex);
        if (!stopping) {
            queues[queueIndex(job.priority)].push_front(std::move(job));  // first in line once its wait is over
            return;
        }
    }
    finish(job, sent, std::move(result));
}

// Never called with `mutex` held: observers and then() callbacks may submit again.
void CurlHttpClient::Impl::finish(Job& job, const Request& sent, HttpResult result) {
    owner.notifyObservers(sent, result);
    job.promise.set(std::move(result));
    inFlight.fetch_sub(1);  // after the callbacks ran, so "nothing in flight" means their results are on their way
}

// After sleep: running transfers are dropped (GETs retry), and a new multi handle means an empty connection cache.
void CurlHttpClient::Impl::resetNow() {
    std::vector<std::unique_ptr<Transfer>> dropped;
    while (!active.empty()) {
        dropped.push_back(detach(active.back().get()));
    }
    if (CURLM* fresh = newMulti()) {
        CURLM* old = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            old = multi;
            multi = fresh;
        }
        curl_multi_cleanup(old);
    }
    for (auto& t : dropped) {
        if (t->job.cancel.cancelled()) {
            finish(t->job, t->sent, cancelledResult("cancelled"));
        } else {
            retryOrFinish(std::move(t->job), t->sent,
                          HttpResult::fail(core::ErrorKind::Network, "the connection was reset (after sleep)"), true);
        }
    }
}

void CurlHttpClient::Impl::stopAll() {
    while (!active.empty()) {
        std::unique_ptr<Transfer> t = detach(active.back().get());
        finish(t->job, t->sent, cancelledResult("the network client has shut down"));
    }
    std::array<std::deque<Job>, kPriorityCount> left;
    CURLM* old = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        left.swap(queues);
        old = multi;
        multi = nullptr;
    }
    for (auto& queue : left) {
        for (Job& job : queue) {
            finish(job, job.request, cancelledResult("the network client has shut down"));
        }
    }
    curl_multi_cleanup(old);
}

CurlHttpClient::CurlHttpClient(CurlOptions options) : impl_(std::make_unique<Impl>(*this, std::move(options))) {}

CurlHttpClient::~CurlHttpClient() {
    shutdown();
}

Future<HttpResult> CurlHttpClient::submit(Request request, Priority priority, CancelToken cancel) {
    Job job;
    job.request = std::move(request);
    job.priority = priority;
    job.cancel = std::move(cancel);
    Future<HttpResult> future = job.promise.future();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->stopping) {
            impl_->inFlight.fetch_add(1);
            impl_->queues[queueIndex(priority)].push_back(std::move(job));
            impl_->wakeLocked();
            return future;
        }
    }
    job.promise.set(cancelledResult(impl_->threadStarted ? "the network client has shut down"
                                                         : "the network thread could not start"));
    return future;
}

void CurlHttpClient::resetConnections() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->resetRequested = true;
    impl_->wakeLocked();
}

void CurlHttpClient::shutdown() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopping = true;
        impl_->wakeLocked();
    }
    std::lock_guard<std::mutex> lock(impl_->joinMutex);
    if (!impl_->threadStarted || impl_->joined || pthread_equal(pthread_self(), impl_->thread)) {
        return;  // nothing to wait for, or called on the network thread itself (it stops on its own)
    }
    pthread_join(impl_->thread, nullptr);
    impl_->joined = true;
}

std::size_t CurlHttpClient::inFlight() const {
    return impl_->inFlight.load();
}

}  // namespace snx::net
