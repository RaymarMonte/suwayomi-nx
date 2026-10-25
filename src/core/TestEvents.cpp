#include "core/TestEvents.hpp"

namespace snx::core::testevents {

std::string line(std::int64_t tMs, std::string_view ev, const nlohmann::ordered_json& fields) {
    nlohmann::ordered_json event = nlohmann::ordered_json::object();
    event["t"] = tMs;
    event["ev"] = std::string(ev);
    if (fields.is_object()) {
        for (const auto& [key, value] : fields.items()) {
            if (key != "t" && key != "ev") {
                event[key] = value;
            }
        }
    }
    // Replace bad UTF-8 instead of throwing: an event must never take the app down.
    return event.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
}

std::string appStart(std::int64_t tMs, const AppStart& info) {
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["version"] = std::string(info.version);
    fields["build"] = std::string(info.build);
    fields["platform"] = std::string(info.platform);
    fields["mode"] = std::string(launchModeName(info.runtime.mode));
    fields["profile"] = std::string(info.profile);
    fields["memTotal"] = info.runtime.totalMem;
    fields["memUsed"] = info.runtime.usedMem;
    return line(tMs, "app_start", fields);
}

std::string screen(std::int64_t tMs, std::string_view name) {
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    fields["name"] = std::string(name);
    return line(tMs, "screen", fields);
}

std::string idle(std::int64_t tMs) {
    return line(tMs, "idle");
}

bool IdleTracker::onFrame(bool busy) {
    if (busy) {
        quietFrames_ = 0;
        reported_ = false;
        return false;
    }
    if (quietFrames_ < kQuietFrames) {
        quietFrames_++;
    }
    if (quietFrames_ >= kQuietFrames && !reported_) {
        reported_ = true;
        return true;
    }
    return false;
}

void IdleTracker::markBusy() {
    quietFrames_ = 0;
    reported_ = false;
}

}  // namespace snx::core::testevents
