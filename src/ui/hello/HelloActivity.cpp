#include "ui/hello/HelloActivity.hpp"

#include <atomic>
#include <memory>
#include <utility>

#include "core/Version.hpp"
#include "core/graphql/Gql.hpp"
#include "core/graphql/Operations.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {

// WP-05's device check: this many requests at once must wait in the queue (6 run at a time), not fail.
constexpr int kBurstSize = 20;

net::Request aboutServerRequest(const std::string& serverUrl) {
    const core::GqlRequest gql = core::gql::aboutServer();
    net::Request request;
    request.method = net::Method::Post;
    request.url = serverUrl + "/api/graphql";
    request.headers = {{"Content-Type", "application/json"}};
    request.body = gql.body();
    request.idempotent = !gql.mutation;  // a query: safe to retry
    return request;
}

// "Suwayomi-Server v2.1.1867 (Stable)", or what went wrong. Pure work, so it may run on the network thread.
std::string describe(const net::HttpResult& result, const core::JsonLimits& limits, bool& ok) {
    ok = false;
    if (!result.ok()) {
        return std::string(core::errorKindName(result.error().kind)) + ": " + result.error().message;
    }
    if (result.value().status != 200) {
        return "HTTP " + std::to_string(result.value().status);
    }
    const auto gql = core::parseGqlResponse(result.value().body, limits);
    if (!gql.ok()) {
        return gql.error().message;
    }
    const auto info = core::gql::readAboutServer(gql.value());
    if (!info.ok()) {
        return info.error().message;
    }
    ok = true;
    return info.value().name + " " + info.value().version + " (" + info.value().buildType + ")";
}

}  // namespace

HelloActivity::HelloActivity(net::HttpClient* http, std::string serverUrl, core::JsonLimits jsonLimits)
    : http_(http), serverUrl_(std::move(serverUrl)), jsonLimits_(jsonLimits) {}

HelloActivity::~HelloActivity() {
    cancel_.cancel();
}

void HelloActivity::onContentAvailable() {
    version->setText("hello/version"_i18n + " " + core::versionLabel());
    burst->setVisibility(brls::Visibility::GONE);
    if (http_ == nullptr || serverUrl_.empty()) {
        server->setVisibility(brls::Visibility::GONE);
        return;
    }
    server->setText("hello/server"_i18n + ": " + "hello/asking"_i18n);
    brls::Logger::info("hello: asking {} for its version", serverUrl_);
    const net::CancelToken cancel = cancel_;
    const core::JsonLimits limits = jsonLimits_;
    http_->submit(aboutServerRequest(serverUrl_), net::Priority::Interactive, cancel)
        .then([this, cancel, limits](const net::HttpResult& result) {
            bool ok = false;
            const std::string text = describe(result, limits, ok);
            // Back on the main thread; `this` is only touched while the screen still exists (it cancels on the way out).
            brls::sync([this, cancel, text, ok] {
                if (cancel.cancelled()) {
                    return;
                }
                brls::Logger::info("hello: server check: {}", text);
                server->setText("hello/server"_i18n + ": " + text);
                if (ok) {
                    startBurst();
                }
            });
        });
}

void HelloActivity::startBurst() {
    burst->setVisibility(brls::Visibility::VISIBLE);
    burst->setText("hello/burst"_i18n + ": " + "hello/asking"_i18n);
    auto answered = std::make_shared<std::atomic<int>>(0);
    auto finished = std::make_shared<std::atomic<int>>(0);
    const net::CancelToken cancel = cancel_;
    const core::JsonLimits limits = jsonLimits_;
    for (int i = 0; i < kBurstSize; ++i) {
        http_->submit(aboutServerRequest(serverUrl_), net::Priority::Background, cancel)
            .then([this, cancel, limits, answered, finished](const net::HttpResult& result) {
                bool ok = false;
                describe(result, limits, ok);
                if (ok) {
                    ++*answered;
                }
                if (++*finished < kBurstSize) {
                    return;
                }
                const int count = answered->load();
                brls::sync([this, cancel, count] {
                    if (cancel.cancelled()) {
                        return;
                    }
                    brls::Logger::info("hello: {} requests at once, {} answered", kBurstSize, count);
                    burst->setText("hello/burst"_i18n + ": " + std::to_string(count) + "/" + std::to_string(kBurstSize));
                });
            });
    }
}

}  // namespace snx::ui
