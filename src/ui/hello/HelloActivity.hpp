#pragma once

#include <borealis.hpp>

namespace snx::ui {

// WP-01's only screen: "Hello Suwayomi" plus the version. Layout: resources/xml/activity/hello.xml.
// WP-08 replaces it with the real app shell.
class HelloActivity : public brls::Activity {
  public:
    CONTENT_FROM_XML_RES("activity/hello.xml");

    void onContentAvailable() override;

  private:
    BRLS_BIND(brls::Label, version, "hello/version");
};

}  // namespace snx::ui
