#include "core/RetryPolicy.hpp"

namespace snx::core {

int retryDelayMs(const RetryPolicy& policy, int retry) {
    if (retry <= 0 || policy.baseDelayMs <= 0 || policy.maxDelayMs <= 0) {
        return 0;
    }
    long long delay = policy.baseDelayMs;
    for (int i = 1; i < retry && delay < policy.maxDelayMs; ++i) {
        delay *= 2;
    }
    return delay < policy.maxDelayMs ? static_cast<int>(delay) : policy.maxDelayMs;
}

bool shouldRetry(const RetryPolicy& policy, int retriesDone, bool idempotent, bool transient) {
    return idempotent && transient && retriesDone < policy.maxRetries;
}

bool isTransientHttpStatus(int status) {
    return status == 408 || status == 429 || status == 502 || status == 503 || status == 504;
}

}  // namespace snx::core
