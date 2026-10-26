#pragma once

#include <string>
#include <string_view>

namespace snx::core {

// The Local source ("<rootDir>/local/" on the server).
inline constexpr std::string_view kLocalSourceId = "0";

// Where a manga comes from. A stub for the MVP: browsing sources and extensions comes later.
struct Source {
    std::string id;  // LongString on the server: a 64-bit number kept as text, because it is only ever compared and sent back
    std::string name;
    std::string displayName;
    std::string lang;
    std::string iconUrl;  // relative to the server, like every other URL it returns
};

}  // namespace snx::core
