#pragma once

#include <borealis.hpp>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/Json.hpp"
#include "image/ImageLoader.hpp"
#include "net/CancelToken.hpp"
#include "net/HttpClient.hpp"

namespace snx::ui {

// WP-07's image benchmark (test builds, the test config's "imageBench"): each URL `repeat` times, twice per round (a page box
// of 2x the screen and a webtoon box of 2x its width), the memory cache emptied before every load so each one decodes.
// The numbers are in the loader's "image" events; `report` gets the summary.
struct ImageBench {
    std::vector<std::string> urls;
    int repeat = 0;
    std::function<void(const nlohmann::ordered_json&)> report;
};

// WP-01's only screen: "Hello Suwayomi" plus the version. Layout: resources/xml/activity/hello.xml.
// WP-05: given a server URL (the test config's), it also asks that server for its version, then sends 20 requests at once to
// check that they queue instead of failing. Both results show under the version and go to the log.
// WP-07: then runs the image benchmark, if there is one. WP-08 replaces this screen with the real app shell.
class HelloActivity : public brls::Activity {
  public:
    CONTENT_FROM_XML_RES("activity/hello.xml");

    // `http` and `images` must outlive the activity (main() owns them). Without `http`, or with an empty URL, there is no
    // server check.
    HelloActivity(net::HttpClient* http = nullptr, std::string serverUrl = {}, core::JsonLimits jsonLimits = {},
                  image::ImageLoader* images = nullptr, ImageBench bench = {});
    ~HelloActivity() override;

    void onContentAvailable() override;

  private:
    struct BenchRun;
    void startBurst();
    void benchNext(const std::shared_ptr<BenchRun>& run);

    BRLS_BIND(brls::Label, version, "hello/version");
    BRLS_BIND(brls::Label, server, "hello/server");
    BRLS_BIND(brls::Label, burst, "hello/burst");

    net::HttpClient* http_;
    std::string serverUrl_;
    core::JsonLimits jsonLimits_;
    image::ImageLoader* images_;
    ImageBench bench_;
    net::CancelToken cancel_;  // cancelled when the screen goes away: late answers are then ignored
};

}  // namespace snx::ui
