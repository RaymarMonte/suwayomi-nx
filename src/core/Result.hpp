#pragma once

#include <string>
#include <utility>
#include <variant>

// "Errors are values, never crashes" (WP-04, SAFETY.md S6): parsers return a Result instead of throwing.
namespace snx::core {

// What kind of problem it was, so the api layer (WP-06) can turn it into its ApiError without reading the text.
enum class ErrorKind {
    Parse,         // the answer isn't usable: not JSON, too big, too deeply nested, or the wrong shape
    Server,        // the server answered with a GraphQL error (errors[] and no usable data)
    Unauthorized,  // a GraphQL error that says the login is missing or has expired (ui_login answers HTTP 200 with it)
    // No usable HTTP answer at all (net, WP-05). An HTTP error status (404, 401, 500) is still an answer, not one of these.
    Network,    // couldn't connect, connection dropped, too many redirects, bad URL
    Timeout,    // connecting or the whole request took longer than its limit
    Cancelled,  // the caller's CancelToken fired, or the HTTP client shut down
    TooLarge,   // the answer was bigger than the request's limit; the download stopped early (SAFETY.md S6)
};

// "parse", "server", "unauthorized", "network", "timeout", "cancelled", "too-large": for logs and test events.
inline const char* errorKindName(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::Parse: return "parse";
        case ErrorKind::Server: return "server";
        case ErrorKind::Unauthorized: return "unauthorized";
        case ErrorKind::Network: return "network";
        case ErrorKind::Timeout: return "timeout";
        case ErrorKind::Cancelled: return "cancelled";
        case ErrorKind::TooLarge: return "too-large";
    }
    return "unknown";
}

struct Error {
    ErrorKind kind = ErrorKind::Parse;
    std::string message;  // plain words, one line, safe to log and to show
};

// Either a value or an Error. Check ok() before value(); value() on an error (or error() on a value) is a bug in the caller.
template <typename T>
class Result {
  public:
    Result(T value) : v_(std::move(value)) {}
    Result(Error error) : v_(std::move(error)) {}

    static Result fail(ErrorKind kind, std::string message) { return Result(Error{kind, std::move(message)}); }

    bool ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }

    const T& value() const& { return std::get<0>(v_); }
    T& value() & { return std::get<0>(v_); }
    T&& value() && { return std::get<0>(std::move(v_)); }
    const Error& error() const { return std::get<1>(v_); }

  private:
    std::variant<T, Error> v_;
};

}  // namespace snx::core
