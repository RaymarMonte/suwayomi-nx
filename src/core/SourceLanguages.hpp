#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/models/Source.hpp"

// The Browse tab's list of sources (WP-14, D56): language names, the order of the rows (Last used, Local, then languages by
// name, sources by name in each) and the languages the user hid. Pure functions, unit-tested (test_core_browse.cpp).
namespace snx::core {

// The Local source's language code on the server (sources { lang }).
inline constexpr std::string_view kLocalSourceLang = "localsourcelang";

// The English name for a source's language code: "en" -> "English", "pt-BR" -> "Portuguese (Brazil)", "all" -> "All
// languages", "other" -> "Other", Local's code -> "Local". A code the table doesn't know comes back as it is.
std::string languageName(std::string_view code);
// The table itself (code, name), for its test: every code once.
const std::vector<std::pair<std::string_view, std::string_view>>& languageTable();

// The Local source: id "0" (or Local's language code).
bool isLocalSource(const Source& source);
// The language a source is listed under (Local's code for the Local source, whatever its lang says).
std::string sourceLanguage(const Source& source);

// One row of the list.
struct SourceRow {
    std::size_t source = 0;  // index into the sources given
    std::string label;       // on the first row of each group: "Last used", "Local", "English", ...; else empty
    bool lastUsed = false;   // the Last used row (the same source shows again in its language)
};

// The rows: the Last used source first (when it is among `sources` and its language isn't hidden), then the Local source,
// then each language by its name, the sources in it by display name (any case, then id). Hidden languages are left out.
std::vector<SourceRow> sourceRows(const std::vector<Source>& sources, std::string_view lastUsedId,
                                  const std::vector<std::string>& hiddenLanguages);

// The languages that have sources (Local's code included), in the list's order: the Languages checklist.
std::vector<std::string> sourceLanguages(const std::vector<Source>& sources);

// The sources Search all asks, in the list's order without the Last used row and hidden languages, at most `max`.
std::vector<std::size_t> searchSources(const std::vector<Source>& sources, const std::vector<std::string>& hiddenLanguages,
                                       std::size_t max);

}  // namespace snx::core
