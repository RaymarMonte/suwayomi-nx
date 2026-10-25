#pragma once

#include <borealis.hpp>
#include <string>

namespace snx::ui {

// The crash screen (SAFETY.md S1): shown when something crashed while the main thread is still fine (a worker thread, or a
// C++ exception that escaped one main-loop iteration). Says a crash record was saved and closes the app on any button, a tap,
// or after 60 s. Layout: resources/xml/activity/crash.xml. (borealis's own Application::crash() is an empty stub.)
class CrashActivity : public brls::Activity {
  public:
    explicit CrashActivity(std::string recordName);

    CONTENT_FROM_XML_RES("activity/crash.xml");

    void onContentAvailable() override;

  private:
    void close();

    std::string recordName_;
    bool closing_ = false;
    BRLS_BIND(brls::Box, box, "crash/box");
    BRLS_BIND(brls::Label, record, "crash/record");
};

}  // namespace snx::ui
