#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace snx::core {

// The Local source ("<rootDir>/local/" on the server).
inline constexpr std::string_view kLocalSourceId = "0";

// The server's ContentWarning for a source (its extension's): Mixed = some of it is for adults, Nsfw = all of it (WP-14 tags
// those "18+"). Names the app doesn't know become Safe.
enum class ContentWarning { Safe, Mixed, Nsfw };
ContentWarning contentWarningFromName(std::string_view name);  // "NSFW" -> Nsfw
std::string_view contentWarningName(ContentWarning warning);    // Nsfw -> "NSFW"

// Where a manga comes from: the Local source or an extension's source (WP-14 browses them).
struct Source {
    std::string id;  // LongString on the server: a 64-bit number kept as text, because it is only ever compared and sent back
    std::string name;
    std::string displayName;  // "SNX Test (EN)"
    std::string lang;         // "en", "pt-BR", "all", Local's "localsourcelang" (core/SourceLanguages.hpp)
    std::string iconUrl;      // relative to the server, like every other URL it returns
    // WP-14. Fields a query didn't ask for keep these defaults.
    bool supportsLatest = false;  // the source has a Latest list
    bool isConfigurable = false;  // the source has settings of its own (not shown yet)
    ContentWarning contentWarning = ContentWarning::Safe;
};

}  // namespace snx::core
