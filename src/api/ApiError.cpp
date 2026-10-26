#include "api/ApiError.hpp"

#include <algorithm>
#include <cctype>

#include "core/graphql/Gql.hpp"

namespace snx::api {

const char* apiErrorKindName(ApiErrorKind kind) {
    switch (kind) {
        case ApiErrorKind::Offline: return "offline";
        case ApiErrorKind::Timeout: return "timeout";
        case ApiErrorKind::Auth: return "auth";
        case ApiErrorKind::Permission: return "permission";
        case ApiErrorKind::Server: return "server";
        case ApiErrorKind::Parse: return "parse";
        case ApiErrorKind::Cancelled: return "cancelled";
    }
    return "unknown";
}

std::string_view userMessage(ApiErrorKind kind) {
    switch (kind) {
        case ApiErrorKind::Offline:
            return "Can't reach the server. Check that it's running and that the Switch is on the same network.";
        case ApiErrorKind::Timeout: return "The server took too long to answer. Try again.";
        case ApiErrorKind::Auth:
            return "The server didn't accept the login. Check the username and password, or log in again.";
        case ApiErrorKind::Permission: return "This account isn't allowed to do that on the server.";
        case ApiErrorKind::Server: return "The server reported a problem.";
        case ApiErrorKind::Parse: return "The server's answer couldn't be read. Is this address a Suwayomi server?";
        case ApiErrorKind::Cancelled: return "Cancelled.";
    }
    return "Something went wrong.";
}

ApiError makeApiError(ApiErrorKind kind, std::string detail) {
    return ApiError{kind, std::string(userMessage(kind)), std::move(detail)};
}

bool looksLikePermissionError(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower.find("permission") != std::string::npos || lower.find("forbidden") != std::string::npos;
}

ApiError fromCoreError(const core::Error& error) {
    switch (error.kind) {
        case core::ErrorKind::Network: return makeApiError(ApiErrorKind::Offline, error.message);
        case core::ErrorKind::Timeout: return makeApiError(ApiErrorKind::Timeout, error.message);
        case core::ErrorKind::Cancelled: return makeApiError(ApiErrorKind::Cancelled, error.message);
        case core::ErrorKind::Unauthorized: return makeApiError(ApiErrorKind::Auth, error.message);
        case core::ErrorKind::TooLarge:
            return ApiError{ApiErrorKind::Parse, "The server's answer was too large for the app.", error.message};
        case core::ErrorKind::Parse: return makeApiError(ApiErrorKind::Parse, error.message);
        case core::ErrorKind::Server:
            break;
    }
    if (looksLikePermissionError(error.message)) {
        return makeApiError(ApiErrorKind::Permission, error.message);
    }
    if (error.message.empty()) {
        return makeApiError(ApiErrorKind::Server);
    }
    return ApiError{ApiErrorKind::Server, "The server reported a problem: " + error.message, error.message};
}

ApiError fromHttpStatus(int status, std::string_view body) {
    // The body's first line, without a Java stack trace, at most 300 bytes: enough for the log.
    const std::string excerpt = body.empty() ? std::string() : core::cleanServerMessage(body);
    std::string detail = "HTTP " + std::to_string(status);
    if (!excerpt.empty()) {
        detail += ": " + excerpt;
    }
    switch (status) {
        case 401: return makeApiError(ApiErrorKind::Auth, std::move(detail));
        case 403: return makeApiError(ApiErrorKind::Permission, std::move(detail));
        case 404:
            return ApiError{ApiErrorKind::Server,
                            "No Suwayomi server answers at this address (HTTP 404). Check the URL, including any sub-path.",
                            std::move(detail)};
        case 408: return makeApiError(ApiErrorKind::Timeout, std::move(detail));
        default:
            return ApiError{ApiErrorKind::Server, "The server reported a problem (HTTP " + std::to_string(status) + ").",
                            std::move(detail)};
    }
}

}  // namespace snx::api
