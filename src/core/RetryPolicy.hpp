#pragma once

// When to try a failed request again, and how long to wait first (WP-05). Pure math, so net and its tests share one answer.
namespace snx::core {

struct RetryPolicy {
    int maxRetries = 2;     // extra tries after the first one
    int baseDelayMs = 250;  // wait before the first retry; it doubles for each retry after that
    int maxDelayMs = 2000;  // the wait never grows past this
};

// How long to wait before retry number `retry` (1 = the first retry): baseDelayMs * 2^(retry-1), capped at maxDelayMs.
// Fixed numbers, no randomness, so tests can rely on them. Zero or negative input gives 0.
int retryDelayMs(const RetryPolicy& policy, int retry);

// Whether to try again after a failed attempt. Only requests that are safe to repeat (`idempotent`: GETs, not POSTs, since a
// repeated mutation could apply twice), only for problems that may go away on their own (`transient`: a dropped connection,
// HTTP 502/503/504), and only while retries are left (`retriesDone` = how many retries already ran).
bool shouldRetry(const RetryPolicy& policy, int retriesDone, bool idempotent, bool transient);

// HTTP answers that mean "busy or briefly down, try again": 408, 429, 502, 503, 504.
bool isTransientHttpStatus(int status);

}  // namespace snx::core
