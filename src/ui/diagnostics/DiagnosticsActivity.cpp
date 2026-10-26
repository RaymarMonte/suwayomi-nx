#include "ui/diagnostics/DiagnosticsActivity.hpp"

#include <atomic>
#include <memory>
#include <utility>

#include "app/TestHooks.hpp"
#include "core/Version.hpp"
#include "core/graphql/Gql.hpp"
#include "core/graphql/Operations.hpp"
#include "platform/Platform.hpp"

using namespace brls::literals;  // for _i18n

namespace snx::ui {

namespace {


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

struct DiagnosticsActivity::BenchRun {
    int index = 0;  // loads done so far
    int ok = 0;
    int failed = 0;
    std::int64_t startMs = 0;
    std::uint64_t memUsedBefore = 0;
};

DiagnosticsActivity::DiagnosticsActivity(net::HttpClient* http, std::string serverUrl, core::JsonLimits jsonLimits,
                                         image::ImageLoader* images, ImageBench bench, int burstSize)
    : http_(http),
      serverUrl_(std::move(serverUrl)),
      jsonLimits_(jsonLimits),
      images_(images),
      bench_(std::move(bench)),
      burstSize_(burstSize) {}

DiagnosticsActivity::~DiagnosticsActivity() {
    cancel_.cancel();
}

void DiagnosticsActivity::onContentAvailable() {
    app::testhooks::screen("Diagnostics");
    version->setText("diagnostics/version"_i18n + " " + core::versionLabel());
    burst->setVisibility(brls::Visibility::GONE);
    if (http_ == nullptr || serverUrl_.empty()) {
        server->setVisibility(brls::Visibility::GONE);
        return;
    }
    server->setText("diagnostics/server"_i18n + ": " + "diagnostics/asking"_i18n);
    brls::Logger::info("diagnostics: asking {} for its version", serverUrl_);
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
                brls::Logger::info("diagnostics: server check: {}", text);
                server->setText("diagnostics/server"_i18n + ": " + text);
                nlohmann::ordered_json fields = nlohmann::ordered_json::object();
                fields["where"] = "diagnostics";
                fields["ok"] = ok;
                fields["version"] = text;
                app::testhooks::write("server_check", fields);
                if (ok) {
                    startBurst();
                    if (images_ != nullptr && bench_.repeat > 0 && !bench_.urls.empty()) {
                        auto run = std::make_shared<BenchRun>();
                        run->startMs = image::steadyMs();
                        run->memUsedBefore = platform::runtimeInfo().usedMem;
                        brls::Logger::info("diagnostics: image benchmark, {} images x {}", bench_.urls.size(), bench_.repeat);
                        benchNext(run);
                    }
                }
            });
        });
}

void DiagnosticsActivity::startBurst() {
    if (burstSize_ <= 0) {
        return;
    }
    burst->setVisibility(brls::Visibility::VISIBLE);
    burst->setText("diagnostics/burst"_i18n + ": " + "diagnostics/asking"_i18n);
    auto answered = std::make_shared<std::atomic<int>>(0);
    auto finished = std::make_shared<std::atomic<int>>(0);
    const net::CancelToken cancel = cancel_;
    const core::JsonLimits limits = jsonLimits_;
    const int size = burstSize_;  // the callbacks run on the network thread: they must not read `this`
    for (int i = 0; i < size; ++i) {
        http_->submit(aboutServerRequest(serverUrl_), net::Priority::Background, cancel)
            .then([this, cancel, limits, answered, finished, size](const net::HttpResult& result) {
                bool ok = false;
                describe(result, limits, ok);
                if (ok) {
                    ++*answered;
                }
                if (++*finished < size) {
                    return;
                }
                const int count = answered->load();
                brls::sync([this, cancel, count, size] {
                    if (cancel.cancelled()) {
                        return;
                    }
                    brls::Logger::info("diagnostics: {} requests at once, {} answered", size, count);
                    burst->setText("diagnostics/burst"_i18n + ": " + std::to_string(count) + "/" + std::to_string(size));
                    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
                    fields["sent"] = size;
                    fields["ok"] = count;
                    app::testhooks::write("net_burst", fields);
                });
            });
    }
}

void DiagnosticsActivity::benchNext(const std::shared_ptr<BenchRun>& run) {
    // Two loads per round: even = a page at 2x the screen, odd = a webtoon strip at 2x the screen's width.
    const int perUrl = bench_.repeat * 2;
    const int total = static_cast<int>(bench_.urls.size()) * perUrl;
    if (run->index >= total) {
        nlohmann::ordered_json summary = nlohmann::ordered_json::object();
        summary["loads"] = total;
        summary["ok"] = run->ok;
        summary["failed"] = run->failed;
        summary["ms"] = image::steadyMs() - run->startMs;
        summary["memUsedBefore"] = run->memUsedBefore;
        summary["memUsedAfter"] = platform::runtimeInfo().usedMem;
        brls::Logger::info("diagnostics: image benchmark done: {} of {} loaded", run->ok, total);
        if (bench_.report) {
            bench_.report(summary);
        }
        return;
    }
    const std::string& url = bench_.urls[static_cast<std::size_t>(run->index / perUrl)];
    const core::ImageSize box = run->index % 2 == 0 ? core::ImageSize{2560, 1440} : core::ImageSize{2560, 0};
    images_->clearMemory();  // so every load decodes (after the first, the bytes come from the disk cache)
    const net::CancelToken cancel = cancel_;
    images_->load(image::LoadRequest{url, box, net::Priority::Interactive, image::ImageKind::Page}, cancel,
                  [this, run, cancel](const image::ImageResult& result) {
                      if (cancel.cancelled()) {
                          return;  // the screen is gone
                      }
                      ++(result.ok() ? run->ok : run->failed);
                      if (!result.ok()) {
                          brls::Logger::warning("diagnostics: image benchmark: {}", result.error().message);
                      }
                      ++run->index;
                      benchNext(run);
                  });
}

}  // namespace snx::ui
