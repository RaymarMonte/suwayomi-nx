#include "core/Json.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <new>

namespace snx::core {

namespace {

// How deeply the text nests objects and arrays, counting only brackets outside strings. Stops early once it is over `limit`.
// Cheap (one pass, no allocation) and done before parsing, so a "[[[[...]]]]" bomb never reaches the parser or any code that
// walks the result recursively.
bool nestingWithin(std::string_view text, int limit) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > limit) {
                return false;
            }
        } else if (c == '}' || c == ']') {
            --depth;  // unbalanced text goes negative; the parser rejects it anyway
        }
    }
    return true;
}

}  // namespace

Result<nlohmann::json> parseJson(std::string_view text, const JsonLimits& limits) {
    if (text.size() > limits.maxBytes) {
        return Result<nlohmann::json>::fail(ErrorKind::Parse, "the answer is too big (" + std::to_string(text.size()) +
                                                                  " bytes, the limit is " + std::to_string(limits.maxBytes) + ")");
    }
    if (!nestingWithin(text, limits.maxDepth)) {
        return Result<nlohmann::json>::fail(ErrorKind::Parse,
                                            "the answer is nested too deeply (limit " + std::to_string(limits.maxDepth) + ")");
    }
    try {
        nlohmann::json value = nlohmann::json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
        if (value.is_discarded()) {
            return Result<nlohmann::json>::fail(ErrorKind::Parse, "the answer is not valid JSON");
        }
        return value;
    } catch (const std::bad_alloc&) {
        return Result<nlohmann::json>::fail(ErrorKind::Parse, "not enough memory to read the answer");
    } catch (const std::exception& e) {
        return Result<nlohmann::json>::fail(ErrorKind::Parse, std::string("the answer could not be read: ") + e.what());
    }
}

std::string dumpJson(const nlohmann::json& value, int indent) {
    return value.dump(indent, ' ', /*ensure_ascii=*/false, nlohmann::json::error_handler_t::replace);
}

namespace json {

const nlohmann::json* field(const nlohmann::json& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return nullptr;
    }
    return &*it;
}

const nlohmann::json* object(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = field(object, key);
    return value && value->is_object() ? value : nullptr;
}

const nlohmann::json* array(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = field(object, key);
    return value && value->is_array() ? value : nullptr;
}

std::optional<std::string> string(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = field(object, key);
    if (!value || !value->is_string()) {
        return std::nullopt;
    }
    return value->get_ref<const std::string&>();
}

std::optional<bool> boolean(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = field(object, key);
    if (!value || !value->is_boolean()) {
        return std::nullopt;
    }
    return value->get<bool>();
}

std::optional<int> asInt32(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        // Both signed and unsigned JSON integers: compare as 64-bit values first.
        if (value.is_number_unsigned()) {
            const auto u = value.get<std::uint64_t>();
            if (u > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                return std::nullopt;
            }
            return static_cast<int>(u);
        }
        const auto i = value.get<std::int64_t>();
        if (i < std::numeric_limits<int>::min() || i > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(i);
    }
    if (value.is_number_float()) {
        const double d = value.get<double>();
        if (!std::isfinite(d) || d != std::floor(d) || d < std::numeric_limits<int>::min() || d > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(d);
    }
    return std::nullopt;
}

std::optional<std::int64_t> asInt64(const nlohmann::json& value) {
    if (value.is_number_unsigned()) {
        const auto u = value.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(u);
    }
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number_float()) {
        const double d = value.get<double>();
        // 2^63 is exactly representable as a double; anything at or above it doesn't fit.
        if (!std::isfinite(d) || d != std::floor(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(d);
    }
    if (value.is_string()) {
        const std::string& s = value.get_ref<const std::string&>();
        std::size_t i = 0;
        const bool negative = !s.empty() && s[0] == '-';
        if (negative) {
            i = 1;
        }
        if (i == s.size() || s.size() - i > 19) {
            return std::nullopt;
        }
        // Accumulate as a negative number, which has room for INT64_MIN.
        std::int64_t result = 0;
        for (; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') {
                return std::nullopt;
            }
            const int digit = s[i] - '0';
            if (result < (std::numeric_limits<std::int64_t>::min() + digit) / 10) {
                return std::nullopt;
            }
            result = result * 10 - digit;
        }
        if (!negative) {
            if (result == std::numeric_limits<std::int64_t>::min()) {
                return std::nullopt;
            }
            result = -result;
        }
        return result;
    }
    return std::nullopt;
}

std::optional<int> int32(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = field(object, key);
    return value ? asInt32(*value) : std::nullopt;
}

std::optional<std::int64_t> int64(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = field(object, key);
    return value ? asInt64(*value) : std::nullopt;
}

std::optional<double> number(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = field(object, key);
    if (!value || !value->is_number()) {
        return std::nullopt;
    }
    const double d = value->get<double>();
    if (!std::isfinite(d)) {
        return std::nullopt;
    }
    return d;
}

std::vector<std::string> strings(const nlohmann::json& object, std::string_view key) {
    std::vector<std::string> out;
    if (const nlohmann::json* list = array(object, key)) {
        for (const auto& entry : *list) {
            if (entry.is_string()) {
                out.push_back(entry.get_ref<const std::string&>());
            }
        }
    }
    return out;
}

}  // namespace json

}  // namespace snx::core
