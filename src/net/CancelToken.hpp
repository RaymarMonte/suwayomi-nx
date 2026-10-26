#pragma once

#include <atomic>
#include <memory>

namespace snx::net {

// A "stop" flag that every copy shares (ARCHITECTURE.md "net"). A screen keeps one, hands copies to its requests and calls
// cancel() when it closes: queued requests are dropped before they start, running ones stop within about 50 ms.
// Any thread may cancel or check it. Once cancelled it stays cancelled; a screen that comes back makes a new token.
class CancelToken {
  public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() const { flag_->store(true); }
    bool cancelled() const { return flag_->load(); }

  private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

}  // namespace snx::net
