#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "core/RuntimeInfo.hpp"

// The pure part of the test hooks (docs/TESTING.md "Test hooks inside the app"): how one event line looks, and when the app
// counts as idle. The app's TestHooks (src/app/) owns the file and the clock and calls these.
namespace snx::core::testevents {

// One JSON Lines entry: {"t":<ms since start>,"ev":"<name>",<fields...>} plus "\n". Fields named "t" or "ev" are dropped,
// so they can't overwrite the two that every event has.
std::string line(std::int64_t tMs, std::string_view ev, const nlohmann::ordered_json& fields = nlohmann::ordered_json::object());

// What the app_start event reports.
struct AppStart {
    std::string_view version;   // "0.0.1"
    std::string_view build;     // "test" or "release"
    std::string_view platform;  // "switch" or "desktop"
    RuntimeInfo runtime;
    std::string_view profile;   // "full" in application mode. "none" in applet mode, which gets no profile (D21)
    bool safeMode = false;      // the last run crashed, so this one runs in safe mode (SAFETY.md S1)
    std::string_view lastCrash; // the crash record behind it ("logs/crashes/crash-....txt"), empty if none
    // WP-08: the profile's main budgets (0 when there is none).
    std::uint64_t imageBudget = 0;    // imageCacheBytes
    std::uint64_t coverBudget = 0;    // coverCacheBytes
    std::uint64_t textureBudget = 0;  // textureBudgetBytes
    std::uint64_t maxDecode = 0;      // maxDecodeBytes
    int httpConcurrency = 0;
    int decodeThreads = 0;
};

std::string appStart(std::int64_t tMs, const AppStart& info);
std::string screen(std::int64_t tMs, std::string_view name);
// {"ev":"screen","name":"Settings","params":{...}}: with what the screen shows that a test checks (WP-08).
std::string screen(std::int64_t tMs, std::string_view name, const nlohmann::ordered_json& params);
std::string idle(std::int64_t tMs);
// {"ev":"launch_refused","mode":"applet","appletType":2}: the only event a run in applet mode writes (D21, WP-08).
std::string launchRefused(std::int64_t tMs, const RuntimeInfo& runtime);
// {"ev":"mem","used":...,"total":...,"free":...}: the allocator's in-use bytes, every 10 s in test builds (WP-08).
std::string mem(std::int64_t tMs, const RuntimeInfo& runtime);
// {"ev":"drill","name":"null-write","phase":"start"}: a crash drill starts (phase "start"), or the cache-write drill is
// writing (phase "writing").
std::string drill(std::int64_t tMs, std::string_view name, std::string_view phase);

// How one HTTP request ended (WP-05). Owns its text, so the network thread can hand it to the main thread.
struct NetEvent {
    std::string method;    // "GET" or "POST"
    std::string url;
    int status = 0;        // the HTTP status, 0 when there was no answer
    int attempts = 0;      // 2 or more after retries
    std::string error;     // "" or an error kind name ("timeout", "cancelled", ...: errorKindName)
};
// {"ev":"net","method":"POST","url":"http://.../api/graphql","status":200,"attempts":1,"error":""}
std::string net(std::int64_t tMs, const NetEvent& event);

// How one image load ended (WP-07). Owns its text, like NetEvent.
struct ImageEvent {
    std::string url;
    std::string source;          // where the bytes came from: "memory" (decoded cache hit), "disk" or "net"
    std::string error;           // "" or the image error kind ("corrupt", "too-large", ...)
    std::string format;          // "jpeg", "png", "webp", "gif", or "" when unknown
    int width = 0;               // the decoded image
    int height = 0;
    int sourceWidth = 0;         // the image as stored
    int sourceHeight = 0;
    int tiles = 0;
    int fetchMs = 0;             // getting the bytes (disk or net)
    int decodeMs = 0;            // decode + downscale + tiling
    std::uint64_t bytes = 0;     // decoded RGBA bytes
    std::uint64_t fileBytes = 0; // the compressed image
    std::uint64_t memUsed = 0;   // the allocator's in-use bytes just after the decode (0 = not measured)
};
// {"ev":"image","url":...,"source":"net","error":"","format":"jpeg","w":720,"h":1080,"srcW":1000,"srcH":1500,"tiles":1,
//  "fetchMs":12,"decodeMs":31,"bytes":3110400,"fileBytes":58437,"memUsed":...}
std::string image(std::int64_t tMs, const ImageEvent& event);

// {"ev":"mem_pressure","usedPct":87,"action":"shrink_caches"} (or "recovered" once use is back down).
std::string memPressure(std::int64_t tMs, int usedPct, std::string_view action);

// Decides when to write an "idle" event: after kQuietFrames frames in a row with nothing running (no animation; later also no
// network or decode job). One idle event per quiet spell: it comes again only after something was busy, or after markBusy().
class IdleTracker {
  public:
    static constexpr int kQuietFrames = 3;

    // Call once per frame. Returns true on the one frame where the app has just become idle.
    bool onFrame(bool busy);

    // Something changed that a test will wait on (a new screen): the next quiet spell reports idle again.
    void markBusy();

  private:
    int quietFrames_ = 0;
    bool reported_ = false;
};

}  // namespace snx::core::testevents
