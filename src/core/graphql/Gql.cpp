#include "core/graphql/Gql.hpp"

#include <cstddef>
#include <utility>

namespace snx::core {

namespace {

constexpr std::size_t kMaxMessageBytes = 300;
constexpr std::string_view kFetchPrefix = "Exception while fetching data (";
constexpr std::string_view kFetchPrefixEnd = ") : ";

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

bool startsWithWord(std::string_view text, std::string_view word) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    return text.substr(0, word.size()) == word;
}

}  // namespace

std::string GqlRequest::body() const {
    nlohmann::json body = nlohmann::json::object();
    body["query"] = std::string(document);
    body["variables"] = variables.is_object() ? variables : nlohmann::json::object();
    return dumpJson(body);
}

GqlRequest makeRequest(const GqlDocument& document) {
    GqlRequest request;
    request.name = document.name;
    request.document = document.text;
    request.mutation = startsWithWord(document.text, "mutation");
    return request;
}

std::string cleanServerMessage(std::string_view raw) {
    std::string_view s = raw;
    if (const std::size_t eol = s.find('\n'); eol != std::string_view::npos) {
        s = s.substr(0, eol);
    }
    if (s.substr(0, kFetchPrefix.size()) == kFetchPrefix) {
        if (const std::size_t end = s.find(kFetchPrefixEnd); end != std::string_view::npos) {
            s.remove_prefix(end + kFetchPrefixEnd.size());
        }
    }
    s = trim(s);
    if (s.size() > kMaxMessageBytes) {
        std::size_t cut = kMaxMessageBytes;
        // Don't cut a UTF-8 character in half: step back over continuation bytes (10xxxxxx).
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        s = s.substr(0, cut);
    }
    // Control characters (escape codes, tabs, a lone \r) would garble a log line or a label.
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        out.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7F ? ' ' : c);
    }
    return out.empty() ? "the server reported an error" : out;
}

bool GqlResponse::unauthorized() const {
    for (const auto& error : errors) {
        if (error.unauthorized) {
            return true;
        }
    }
    return false;
}

Error GqlResponse::firstError() const {
    if (errors.empty()) {
        return Error{ErrorKind::Server, "the server sent no data"};
    }
    // An unauthorized error wins: it tells the api layer to log in again, whatever else went wrong.
    for (const auto& error : errors) {
        if (error.unauthorized) {
            return Error{ErrorKind::Unauthorized, error.message};
        }
    }
    return Error{ErrorKind::Server, errors.front().message};
}

Result<GqlResponse> parseGqlResponse(std::string_view body, const JsonLimits& limits) {
    Result<nlohmann::json> parsed = parseJson(body, limits);
    if (!parsed.ok()) {
        return parsed.error();
    }
    nlohmann::json& root = parsed.value();  // not const: "data" is moved out, a big answer isn't copied
    if (!root.is_object()) {
        return Result<GqlResponse>::fail(ErrorKind::Parse, "the answer is not a JSON object");
    }
    const auto data = root.find("data");
    const auto errors = root.find("errors");
    if (data == root.end() && errors == root.end()) {
        return Result<GqlResponse>::fail(ErrorKind::Parse, "the answer has neither data nor errors");
    }
    GqlResponse response;
    if (data != root.end()) {
        response.data = std::move(*data);
    }
    if (errors != root.end() && errors->is_array()) {
        for (const auto& entry : *errors) {
            GqlError error;
            error.message = cleanServerMessage(json::string(entry, "message").value_or(""));
            error.unauthorized = error.message == "Unauthorized";
            if (const nlohmann::json* path = json::array(entry, "path")) {
                for (const auto& part : *path) {
                    if (!error.path.empty()) {
                        error.path += '/';
                    }
                    if (part.is_string()) {
                        error.path += part.get_ref<const std::string&>();
                    } else if (const auto index = json::asInt64(part)) {
                        error.path += std::to_string(*index);
                    }
                }
            }
            response.errors.push_back(std::move(error));
        }
    } else if (errors != root.end() && !errors->is_null()) {
        response.errors.push_back(GqlError{"the server reported an error in an unknown form", "", false});
    }
    return response;
}

}  // namespace snx::core
