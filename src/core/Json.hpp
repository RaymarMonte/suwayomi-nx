#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Result.hpp"

// The one way core reads JSON from outside the app (server answers, config.json): with size and nesting limits (SAFETY.md S6),
// and never throwing. Plus small readers that turn a missing, null or wrong-typed field into "nothing" instead of an exception,
// which is how the models stay tolerant (WP-04).
namespace snx::core {

// S6 limits. The defaults are the S6 numbers; RuntimeProfile carries the values the app uses (profile.json).
struct JsonLimits {
    std::size_t maxBytes = 8u * 1024 * 1024;  // a bigger answer is refused before parsing
    int maxDepth = 64;                        // nested objects/arrays; Suwayomi's answers go about 8 deep
};

// Parses text into JSON. Fails (ErrorKind::Parse) if it is over the size limit, nested deeper than the limit, not valid JSON
// (invalid UTF-8 included), or if parsing runs out of memory. Never throws.
Result<nlohmann::json> parseJson(std::string_view text, const JsonLimits& limits = {});

// Turns any JSON value into text without ever throwing: invalid UTF-8 in strings is replaced (U+FFFD).
// indent < 0 = one line.
std::string dumpJson(const nlohmann::json& value, int indent = -1);

// Field readers. `object` may be anything (a non-object just has no fields). They return nothing when the field is missing,
// null, of the wrong type or out of range, and never throw.
namespace json {

const nlohmann::json* field(const nlohmann::json& object, std::string_view key);
// The field if it is an object / an array.
const nlohmann::json* object(const nlohmann::json& object, std::string_view key);
const nlohmann::json* array(const nlohmann::json& object, std::string_view key);

std::optional<std::string> string(const nlohmann::json& object, std::string_view key);
std::optional<bool> boolean(const nlohmann::json& object, std::string_view key);
// GraphQL Int: a whole JSON number that fits in 32 bits (3.0 counts, 3.5 doesn't).
std::optional<int> int32(const nlohmann::json& object, std::string_view key);
// GraphQL LongString: a string of digits with an optional "-" ("1767312180000"), or a whole JSON number, that fits in 64 bits.
std::optional<std::int64_t> int64(const nlohmann::json& object, std::string_view key);
// GraphQL Float: any finite JSON number.
std::optional<double> number(const nlohmann::json& object, std::string_view key);
// A list of strings. Entries that aren't strings are dropped. Missing / not a list = empty.
std::vector<std::string> strings(const nlohmann::json& object, std::string_view key);

// The same for a value on its own (array entries).
std::optional<int> asInt32(const nlohmann::json& value);
std::optional<std::int64_t> asInt64(const nlohmann::json& value);

}  // namespace json

}  // namespace snx::core
