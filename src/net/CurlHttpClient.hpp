#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "core/RetryPolicy.hpp"
#include "core/RuntimeProfile.hpp"
#include "net/HttpClient.hpp"

namespace snx::net {

// Everything CurlHttpClient needs to know. Use curlOptionsFrom(profile): the numbers belong to the RuntimeProfile.
struct CurlOptions {
    int maxConcurrent = 1;                 // transfers at once (httpConcurrency)
    std::uint64_t defaultMaxBodyBytes = 0; // when a request sets none (json.maxBytes)
    std::uint64_t maxHeaderBytes = 0;      // all headers of one answer (httpMaxHeaderBytes)
    int connectTimeoutMs = 0;              // when a request sets none (httpConnectTimeoutMs)
    int timeoutMs = 0;                     // when a request sets none (httpTimeoutMs)
    int maxRedirects = 0;                  // httpMaxRedirects
    core::RetryPolicy retry;               // httpRetry
    std::uint64_t threadStackBytes = 0;    // netThreadStackBytes
    std::string userAgent;                 // "suwayomi-nx/<version>"
};

CurlOptions curlOptionsFrom(const core::RuntimeProfile& profile);

// The real HttpClient (D5): one network thread drives a libcurl "multi" handle, so many transfers share one thread and few
// sockets. At most maxConcurrent transfers run; the rest wait in priority order. Kept-alive connections are reused, answers
// are un-gzipped, redirects are followed (http/https only, limited), and the body limit is checked while the answer streams
// in: an answer that grows past it stops right there (TooLarge), it is never buffered whole first (SAFETY.md S6).
// Failed GETs (and POSTs marked idempotent) are retried by core::RetryPolicy. Needs the socket service up (borealis starts it
// on the Switch).
class CurlHttpClient final : public HttpClient {
  public:
    explicit CurlHttpClient(CurlOptions options);
    ~CurlHttpClient() override;

    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;

    Future<HttpResult> submit(Request request, Priority priority, CancelToken cancel) override;
    void resetConnections() override;
    void shutdown() override;
    std::size_t inFlight() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;  // keeps curl.h out of this header
};

}  // namespace snx::net
