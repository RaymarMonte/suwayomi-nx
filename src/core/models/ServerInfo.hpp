#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace snx::core {

// aboutServer: which Suwayomi-Server answers.
struct ServerInfo {
    std::string name;        // "Suwayomi-Server"
    std::string version;     // "v2.4.2366"
    std::string buildType;   // "Stable" or "Preview"
    std::int64_t buildTime = 0;  // seconds since 1970 (UTC), 0 = unknown
};

// "v2.4.2366" as numbers. known = false when the text doesn't look like a version.
struct ServerVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool known = false;
};

// Reads "v2.4.2366", "2.4.2366", "v2.4" (patch 0) or "v2.4.2366-preview" (the part after "-" is ignored).
ServerVersion parseServerVersion(std::string_view text);

// The oldest server the app supports (D9): the stable release the schema was dumped from in WP-02.
inline constexpr ServerVersion kMinimumServerVersion{2, 4, 2366, true};

// a >= b, both known.
bool versionAtLeast(const ServerVersion& a, const ServerVersion& b);

// D9: the app warns (but still runs) when this is false. An unknown version counts as not supported.
bool isSupportedServer(const ServerInfo& info);

}  // namespace snx::core
