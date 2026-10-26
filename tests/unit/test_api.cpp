#include <doctest/doctest.h>

#include <set>
#include <string>

#include "api/Api.hpp"
#include "api/ApiError.hpp"
#include "api/ServerUrl.hpp"
#include "api/SuwayomiClient.hpp"

// The api module's pure helpers: errors and their messages, server URLs, the Basic header, the D9 version warning.
// SuwayomiClient itself: test_api_client.cpp (fake HTTP) and test_api_curl.cpp (real HTTP on 127.0.0.1).
using namespace snx::api;
using snx::core::ErrorKind;

TEST_SUITE("api") {

TEST_CASE("the User-Agent names the app and its version") {
    CHECK(userAgent() == std::string("suwayomi-nx/") + SNX_TEST_EXPECTED_VERSION);
}

TEST_CASE("ApiError: every kind has its own name and its own message") {
    const ApiErrorKind kinds[] = {ApiErrorKind::Offline, ApiErrorKind::Timeout,   ApiErrorKind::Auth,
                                  ApiErrorKind::Permission, ApiErrorKind::Server, ApiErrorKind::Parse,
                                  ApiErrorKind::Cancelled};
    std::set<std::string> names;
    std::set<std::string> messages;
    for (ApiErrorKind kind : kinds) {
        names.insert(apiErrorKindName(kind));
        messages.insert(std::string(userMessage(kind)));
        CHECK_FALSE(userMessage(kind).empty());
        const ApiError error = makeApiError(kind, "why");
        CHECK(error.kind == kind);
        CHECK(error.message == userMessage(kind));
        CHECK(error.detail == "why");
    }
    CHECK(names.size() == 7);
    CHECK(messages.size() == 7);
    CHECK(std::string(apiErrorKindName(ApiErrorKind::Offline)) == "offline");
    CHECK(std::string(apiErrorKindName(ApiErrorKind::Permission)) == "permission");
}

TEST_CASE("ApiError: core and net errors map to the kind a screen needs") {
    struct Row {
        ErrorKind from;
        ApiErrorKind to;
    };
    const Row rows[] = {
        {ErrorKind::Network, ApiErrorKind::Offline},     {ErrorKind::Timeout, ApiErrorKind::Timeout},
        {ErrorKind::Cancelled, ApiErrorKind::Cancelled}, {ErrorKind::Unauthorized, ApiErrorKind::Auth},
        {ErrorKind::TooLarge, ApiErrorKind::Parse},      {ErrorKind::Parse, ApiErrorKind::Parse},
        {ErrorKind::Server, ApiErrorKind::Server},
    };
    for (const Row& row : rows) {
        CAPTURE(snx::core::errorKindName(row.from));
        const ApiError error = fromCoreError({row.from, "curl said no"});
        CHECK(error.kind == row.to);
        CHECK(error.detail == "curl said no");
        CHECK_FALSE(error.message.empty());
    }
    SUBCASE("a server text is shown to the user") {
        const ApiError error = fromCoreError({ErrorKind::Server, "Manga not found"});
        CHECK(error.message == "The server reported a problem: Manga not found");
    }
    SUBCASE("a server error about permissions becomes Permission") {
        CHECK(fromCoreError({ErrorKind::Server, "Missing permission DOWNLOAD_CHAPTERS"}).kind == ApiErrorKind::Permission);
        CHECK(fromCoreError({ErrorKind::Server, "Forbidden"}).kind == ApiErrorKind::Permission);
    }
    SUBCASE("a server error without text gets the standard message") {
        CHECK(fromCoreError({ErrorKind::Server, ""}).message == userMessage(ApiErrorKind::Server));
    }
}

TEST_CASE("ApiError: HTTP statuses") {
    CHECK(fromHttpStatus(401).kind == ApiErrorKind::Auth);
    CHECK(fromHttpStatus(403).kind == ApiErrorKind::Permission);
    CHECK(fromHttpStatus(408).kind == ApiErrorKind::Timeout);
    CHECK(fromHttpStatus(500).kind == ApiErrorKind::Server);
    CHECK(fromHttpStatus(502).kind == ApiErrorKind::Server);
    CHECK(fromHttpStatus(429).kind == ApiErrorKind::Server);
    const ApiError notFound = fromHttpStatus(404, "Not Found");
    CHECK(notFound.kind == ApiErrorKind::Server);
    CHECK(notFound.message.find("sub-path") != std::string::npos);
    CHECK(notFound.detail == "HTTP 404: Not Found");
    // The detail keeps the first line only (Suwayomi appends a Java stack trace).
    const ApiError crash = fromHttpStatus(500, "Internal error\r\n\tat java.lang.Thread.run");
    CHECK(crash.detail == "HTTP 500: Internal error");
    CHECK(crash.message == "The server reported a problem (HTTP 500).");
    CHECK(fromHttpStatus(401).detail == "HTTP 401");
}

TEST_CASE("looksLikePermissionError") {
    CHECK(looksLikePermissionError("You don't have PERMISSION to do that"));
    CHECK(looksLikePermissionError("Forbidden"));
    CHECK_FALSE(looksLikePermissionError("Unauthorized"));
    CHECK_FALSE(looksLikePermissionError(""));
}

TEST_CASE("normalizeServerUrl: what the user typed -> the base URL") {
    CHECK(normalizeServerUrl("192.168.1.10:4567") == "http://192.168.1.10:4567");
    CHECK(normalizeServerUrl("  http://192.168.1.10:4567/  ") == "http://192.168.1.10:4567");
    CHECK(normalizeServerUrl("https://NAS.local/Suwayomi//") == "https://nas.local/Suwayomi");
    CHECK(normalizeServerUrl("HTTP://Host:4567") == "http://host:4567");
    CHECK(normalizeServerUrl("http://host:4567/api/graphql") == "http://host:4567");
    CHECK(normalizeServerUrl("http://host/sub/api/graphql/") == "http://host/sub");
    CHECK(normalizeServerUrl("") == "");
    CHECK(normalizeServerUrl(" \t ") == "");
}

TEST_CASE("graphqlUrl and absoluteUrl") {
    CHECK(graphqlUrl("http://h:4567") == "http://h:4567/api/graphql");
    CHECK(graphqlUrl("http://h/sub") == "http://h/sub/api/graphql");

    CHECK(absoluteUrl("http://h:4567", "/api/v1/manga/1/thumbnail") == "http://h:4567/api/v1/manga/1/thumbnail");
    // A sub-path stays, and there is exactly one "/" between base and path, whatever either side brings.
    CHECK(absoluteUrl("http://h/suwayomi", "/api/v1/manga/1/thumbnail") == "http://h/suwayomi/api/v1/manga/1/thumbnail");
    CHECK(absoluteUrl("http://h/suwayomi/", "api/v1/x") == "http://h/suwayomi/api/v1/x");
    CHECK(absoluteUrl("http://h/suwayomi//", "//api/v1/x") == "http://h/suwayomi/api/v1/x");
    // The query (pages converted by the server, D6) stays exactly as the server sent it.
    CHECK(absoluteUrl("http://h:4567", "/api/v1/manga/3/chapter/1/page/0?format=image%2Fjpeg") ==
          "http://h:4567/api/v1/manga/3/chapter/1/page/0?format=image%2Fjpeg");
    // Already a full URL (some sources' icons): unchanged.
    CHECK(absoluteUrl("http://h:4567", "https://cdn.example/icon.png") == "https://cdn.example/icon.png");
    CHECK(absoluteUrl("http://h:4567", "HTTP://cdn.example/a") == "HTTP://cdn.example/a");
    CHECK(absoluteUrl("http://h:4567", "") == "");
}

TEST_CASE("isServerUrl") {
    CHECK(isServerUrl("http://h:4567", "http://h:4567/api/v1/manga/1/thumbnail"));
    CHECK(isServerUrl("http://h:4567", "http://h:4567"));
    CHECK(isServerUrl("http://h:4567", "http://h:4567?x=1"));
    CHECK(isServerUrl("http://h/sub", "http://h/sub/api/graphql"));
    CHECK_FALSE(isServerUrl("http://h:4567", "http://h:45678/api"));
    CHECK_FALSE(isServerUrl("http://h/sub", "http://h/subway/api"));
    CHECK_FALSE(isServerUrl("http://h:4567", "https://cdn.example/icon.png"));
    CHECK_FALSE(isServerUrl("", "http://h:4567/api"));
}

TEST_CASE("basicAuthHeader is RFC 7617's Basic header") {
    CHECK(basicAuthHeader("Aladdin", "open sesame") == "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");  // the RFC's example
    CHECK(basicAuthHeader("a", "") == "Basic YTo=");       // one byte left over
    CHECK(basicAuthHeader("ab", "") == "Basic YWI6");      // none left over
    CHECK(basicAuthHeader("a", "bc") == "Basic YTpiYw==");  // two padding characters
}

TEST_CASE("serverWarning: D9's minimum version") {
    snx::core::ServerInfo info;
    info.name = "Suwayomi-Server";
    info.version = "v2.4.2366";
    CHECK_FALSE(serverWarning(info).has_value());
    info.version = "v2.5.0";
    CHECK_FALSE(serverWarning(info).has_value());
    info.version = "v2.4.2000";
    REQUIRE(serverWarning(info).has_value());
    CHECK(serverWarning(info)->find("v2.4.2000") != std::string::npos);
    CHECK(serverWarning(info)->find("v2.4.2366") != std::string::npos);
    info.version = "";
    REQUIRE(serverWarning(info).has_value());
    CHECK(serverWarning(info)->find("didn't say") != std::string::npos);
}

}  // TEST_SUITE("api")
