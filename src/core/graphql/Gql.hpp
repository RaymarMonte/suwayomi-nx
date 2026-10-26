#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "core/Json.hpp"
#include "core/Result.hpp"

// GraphQL requests and answers (D4, WP-04). The documents themselves are src/core/graphql/*.graphql, embedded at build time
// (Documents.hpp). Operations.hpp pairs each one with its variables and its answer reader; the api layer (WP-06) sends them.
namespace snx::core {

// One embedded .graphql file. name = file name = operation name ("AboutServer").
struct GqlDocument {
    std::string_view name;
    std::string_view text;
};

struct GqlRequest {
    std::string_view name;      // for logs
    std::string_view document;  // the embedded text
    nlohmann::json variables = nlohmann::json::object();
    bool mutation = false;      // changes server state (don't repeat it blindly)

    // The POST body for {base}/api/graphql: {"query": ..., "variables": {...}}. Never throws (bad UTF-8 is replaced).
    std::string body() const;
};

// A request for an embedded document with no variables yet.
GqlRequest makeRequest(const GqlDocument& document);

struct GqlError {
    std::string message;        // cleaned up (cleanServerMessage): one line, fit to show
    std::string path;           // where it happened, e.g. "manga" or "fetchChapterPages/pages"; empty if the server didn't say
    bool unauthorized = false;  // missing or expired login (ui_login): get a new token, or log in again
};

// The server answers HTTP 200 with {"data": ..., "errors": [...]}: data can be null, complete, or partial next to errors.
struct GqlResponse {
    nlohmann::json data;  // null when the server sent none
    std::vector<GqlError> errors;

    bool unauthorized() const;  // any error is an unauthorized one
    // The first error as an Error (kind Unauthorized or Server), for when the data is unusable. A generic one if there are none.
    Error firstError() const;
};

// Reads an answer body. Fails (ErrorKind::Parse) if it is over the limits, not JSON, not an object, or has neither "data" nor
// "errors". Never throws.
Result<GqlResponse> parseGqlResponse(std::string_view body, const JsonLimits& limits = {});

// Suwayomi error messages carry a Java stack trace and a prefix (SUWAYOMI-API.md):
// "Exception while fetching data (/login) : Incorrect username or password.\r\n\r\njava.lang.Exception: ..."
// -> "Incorrect username or password." Keeps the first line only, at most 300 bytes (cut between UTF-8 characters).
std::string cleanServerMessage(std::string_view raw);

}  // namespace snx::core
