#pragma once

#include "api/SuwayomiClient.hpp"
#include "app/ConfigStore.hpp"
#include "app/crash/Crash.hpp"
#include "core/RuntimeInfo.hpp"
#include "core/RuntimeProfile.hpp"
#include "image/ImageLoader.hpp"
#include "net/HttpClient.hpp"
#include "platform/SafeFs.hpp"

// Everything the screens share (WP-08), made once in main() in this order and handed to each screen as `AppContext&`. No
// globals: a screen gets what it needs from here. Everything in it outlives every screen (borealis deletes the screens
// inside its last main-loop iteration, before main() shuts these down).
namespace snx::app {

struct AppContext {
    const platform::SafeFs& fs;        // the write jail rooted at the app's folder (D14)
    const core::RuntimeInfo runtime;   // launch mode and memory at start
    const core::RuntimeProfile profile;  // built once from `runtime` (D21: Full), halved in safe mode
    const crash::LastRun lastRun;      // the last run's crash, if any: this run is in safe mode
    ConfigStore& config;               // config.json
    net::HttpClient& http;
    api::SuwayomiClient& client;       // the server, with the login from config.server
    image::ImageLoader& images;
};

}  // namespace snx::app
