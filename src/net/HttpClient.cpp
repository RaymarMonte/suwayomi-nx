#include "net/HttpClient.hpp"

#include <cctype>

namespace snx::net {

const char* priorityName(Priority priority) {
    switch (priority) {
        case Priority::Interactive: return "interactive";
        case Priority::Reader: return "reader";
        case Priority::Prefetch: return "prefetch";
        case Priority::Background: return "background";
    }
    return "unknown";
}

std::string Response::header(std::string_view name) const {
    for (const auto& [key, value] : headers) {
        if (key.size() != name.size()) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < key.size() && same; ++i) {
            same = std::tolower(static_cast<unsigned char>(key[i])) == std::tolower(static_cast<unsigned char>(name[i]));
        }
        if (same) {
            return value;
        }
    }
    return {};
}

void HttpClient::setRequestDecorator(RequestDecorator decorator) {
    std::lock_guard<std::mutex> lock(hooksMutex_);
    decorator_ = std::move(decorator);
}

void HttpClient::addResponseObserver(ResponseObserver observer) {
    std::lock_guard<std::mutex> lock(hooksMutex_);
    observers_.push_back(std::move(observer));
}

// The hooks are copied out and called without the lock, so a hook may itself set hooks without deadlocking.
void HttpClient::decorate(Request& request) const {
    RequestDecorator decorator;
    {
        std::lock_guard<std::mutex> lock(hooksMutex_);
        decorator = decorator_;
    }
    if (decorator) {
        decorator(request);
    }
}

void HttpClient::notifyObservers(const Request& request, const HttpResult& result) const {
    std::vector<ResponseObserver> observers;
    {
        std::lock_guard<std::mutex> lock(hooksMutex_);
        observers = observers_;
    }
    for (const auto& observer : observers) {
        observer(request, result);
    }
}

}  // namespace snx::net
