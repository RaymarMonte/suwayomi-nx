#include "api/ServerUrl.hpp"

#include <cctype>

namespace snx::api {

namespace {

constexpr std::string_view kGraphqlPath = "/api/graphql";

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool startsWithNoCase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lower(text[i]) != lower(prefix[i])) {
            return false;
        }
    }
    return true;
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

void dropTrailingSlashes(std::string& url, std::size_t keep) {
    while (url.size() > keep && url.back() == '/') {
        url.pop_back();
    }
}

}  // namespace

std::string normalizeServerUrl(std::string_view typed) {
    while (!typed.empty() && isSpace(typed.front())) {
        typed.remove_prefix(1);
    }
    while (!typed.empty() && isSpace(typed.back())) {
        typed.remove_suffix(1);
    }
    if (typed.empty()) {
        return {};
    }
    std::string url(typed);
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        url = "http://" + url;
        schemeEnd = 4;
    }
    // Scheme and host are case-insensitive; the path is not, so it stays as typed.
    const std::size_t hostStart = schemeEnd + 3;
    std::size_t hostEnd = url.find_first_of("/?#", hostStart);
    if (hostEnd == std::string::npos) {
        hostEnd = url.size();
    }
    for (std::size_t i = 0; i < hostEnd; ++i) {
        url[i] = lower(url[i]);
    }
    dropTrailingSlashes(url, hostStart);
    if (endsWith(url, kGraphqlPath)) {
        url.resize(url.size() - kGraphqlPath.size());
        dropTrailingSlashes(url, hostStart);
    }
    return url;
}

std::string graphqlUrl(std::string_view baseUrl) {
    std::string url(baseUrl);
    url += kGraphqlPath;
    return url;
}

std::string absoluteUrl(std::string_view baseUrl, std::string_view path) {
    if (path.empty()) {
        return {};
    }
    if (startsWithNoCase(path, "http://") || startsWithNoCase(path, "https://")) {
        return std::string(path);
    }
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.remove_suffix(1);
    }
    while (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    std::string url(baseUrl);
    url += '/';
    url += path;
    return url;
}

bool isServerUrl(std::string_view baseUrl, std::string_view url) {
    if (baseUrl.empty() || url.size() < baseUrl.size() || url.substr(0, baseUrl.size()) != baseUrl) {
        return false;
    }
    if (url.size() == baseUrl.size()) {
        return true;
    }
    const char next = url[baseUrl.size()];
    return next == '/' || next == '?';
}

}  // namespace snx::api
