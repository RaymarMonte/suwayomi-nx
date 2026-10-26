#pragma once

#include <borealis.hpp>
#include <string>

#include "core/Json.hpp"
#include "net/CancelToken.hpp"
#include "net/HttpClient.hpp"

namespace snx::ui {

// WP-01's only screen: "Hello Suwayomi" plus the version. Layout: resources/xml/activity/hello.xml.
// WP-05: given a server URL (the test config's), it also asks that server for its version, then sends 20 requests at once to
// check that they queue instead of failing. Both results show under the version and go to the log.
// WP-08 replaces it with the real app shell.
class HelloActivity : public brls::Activity {
  public:
    CONTENT_FROM_XML_RES("activity/hello.xml");

    // `http` must outlive the activity (main() owns both). Without it, or with an empty URL, there is no server check.
    HelloActivity(net::HttpClient* http = nullptr, std::string serverUrl = {}, core::JsonLimits jsonLimits = {});
    ~HelloActivity() override;

    void onContentAvailable() override;

  private:
    void startBurst();

    BRLS_BIND(brls::Label, version, "hello/version");
    BRLS_BIND(brls::Label, server, "hello/server");
    BRLS_BIND(brls::Label, burst, "hello/burst");

    net::HttpClient* http_;
    std::string serverUrl_;
    core::JsonLimits jsonLimits_;
    net::CancelToken cancel_;  // cancelled when the screen goes away: late answers are then ignored
};

}  // namespace snx::ui
