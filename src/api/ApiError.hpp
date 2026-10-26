#pragma once

#include <string>
#include <string_view>

#include "core/Result.hpp"

// What can go wrong when the app talks to Suwayomi-Server, in the words a screen needs (WP-06, ARCHITECTURE.md "Error
// handling"). Every SuwayomiClient call ends with a value or one of these; nothing throws.
namespace snx::api {

enum class ApiErrorKind {
    Offline,     // no answer at all: wrong address, server not running, Wi-Fi off, connection dropped
    Timeout,     // the server took longer than the request's time limit
    Auth,        // the login is missing, wrong or expired and couldn't be renewed: the user has to log in again
    Permission,  // logged in, but this user may not do that (multi-user servers)
    Server,      // the server answered with an error (a GraphQL error, or an HTTP error such as 404 or 500)
    Parse,       // the answer isn't one the app can read (not Suwayomi, too big, broken JSON)
    Cancelled,   // the caller's CancelToken fired (the screen went away). Never shown to the user
};

// "offline", "timeout", "auth", "permission", "server", "parse", "cancelled": for logs and test events.
const char* apiErrorKindName(ApiErrorKind kind);

// The standard sentence a screen shows for each kind, e.g. "Can't reach the server. ...".
std::string_view userMessage(ApiErrorKind kind);

struct ApiError {
    ApiErrorKind kind = ApiErrorKind::Server;
    std::string message;  // for the screen: one line in plain words (userMessage(kind), or a better one when known)
    std::string detail;   // for the log: what exactly happened ("HTTP 500", the server's or curl's text)
};

// A value or an ApiError.
template <typename T>
using ApiResult = core::Result<T, ApiError>;

// An ApiError with the standard message for its kind.
ApiError makeApiError(ApiErrorKind kind, std::string detail = {});

// core and net errors -> ApiError: Network -> Offline, Timeout -> Timeout, Cancelled -> Cancelled, Unauthorized -> Auth,
// TooLarge and Parse -> Parse, Server -> Server (or Permission when the server's text says so). A server text becomes the
// message.
ApiError fromCoreError(const core::Error& error);

// An HTTP answer that isn't 200 -> ApiError: 401 -> Auth, 403 -> Permission, 408 -> Timeout, anything else -> Server.
// body: the answer, used for the detail (its first line, short).
ApiError fromHttpStatus(int status, std::string_view body = {});

// True if a server error text means "this user may not do that" (the server says "permission" or "forbidden").
bool looksLikePermissionError(std::string_view text);

}  // namespace snx::api
