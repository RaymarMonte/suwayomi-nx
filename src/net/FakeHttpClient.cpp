#include "net/FakeHttpClient.hpp"

#include <algorithm>

namespace snx::net {

HttpResult FakeHttpClient::answer(int status, std::string body, std::vector<Header> headers) {
    Response response;
    response.status = status;
    response.body = std::move(body);
    response.headers = std::move(headers);
    return HttpResult(std::move(response));
}

void FakeHttpClient::setHandler(Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handler_ = std::move(handler);
}

void FakeHttpClient::hold(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    holding_ = on;
}

bool FakeHttpClient::releaseNext() {
    Waiting item;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (waiting_.empty()) {
            return false;
        }
        // Most urgent first; among equals the oldest (stable search keeps submit order).
        auto it = std::min_element(waiting_.begin(), waiting_.end(), [](const Waiting& a, const Waiting& b) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        });
        item = std::move(*it);
        waiting_.erase(it);
    }
    complete(item);
    return true;
}

std::size_t FakeHttpClient::releaseAll() {
    std::size_t count = 0;
    while (releaseNext()) {
        ++count;
    }
    return count;
}

std::size_t FakeHttpClient::waitingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return waiting_.size();
}

std::vector<Request> FakeHttpClient::requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

int FakeHttpClient::resetCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resets_;
}

Future<HttpResult> FakeHttpClient::submit(Request request, Priority priority, CancelToken cancel) {
    Waiting item{std::move(request), priority, std::move(cancel), {}};
    Future<HttpResult> future = item.promise.future();
    decorate(item.request);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            item.promise.set(HttpResult::fail(core::ErrorKind::Cancelled, "the network client has shut down"));
            return future;
        }
        requests_.push_back(item.request);
        if (holding_) {
            waiting_.push_back(std::move(item));
            return future;
        }
    }
    complete(item);
    return future;
}

void FakeHttpClient::complete(Waiting& item) {
    Handler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler = handler_;
    }
    HttpResult result = item.cancel.cancelled() ? HttpResult::fail(core::ErrorKind::Cancelled, "cancelled")
                        : handler              ? handler(item.request)
                                               : answer(404);
    notifyObservers(item.request, result);
    item.promise.set(std::move(result));
}

void FakeHttpClient::resetConnections() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++resets_;
}

void FakeHttpClient::shutdown() {
    std::deque<Waiting> left;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        left.swap(waiting_);
    }
    for (Waiting& item : left) {
        HttpResult result = HttpResult::fail(core::ErrorKind::Cancelled, "the network client has shut down");
        notifyObservers(item.request, result);
        item.promise.set(std::move(result));
    }
}

}  // namespace snx::net
