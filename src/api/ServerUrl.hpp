#pragma once

#include <string>
#include <string_view>

// Server addresses (WP-06, SUWAYOMI-API.md "Transport"). The server may sit behind a reverse proxy under a sub-path
// ("http://nas/suwayomi"), and it returns page and thumbnail URLs relative to itself ("/api/v1/manga/1/thumbnail").
namespace snx::api {

// What the user typed -> the base URL every request starts with: spaces trimmed, "http://" added when there is no scheme,
// scheme and host in lower case, trailing slashes dropped, and a pasted "/api/graphql" removed.
// "192.168.1.10:4567/" -> "http://192.168.1.10:4567". "" stays "".
std::string normalizeServerUrl(std::string_view typed);

// {base}/api/graphql, where every GraphQL request goes.
std::string graphqlUrl(std::string_view baseUrl);

// A path the server returned -> a full URL on the server: the base URL (with its sub-path), one "/", then the path. The query
// stays exactly as it is ("/api/v1/manga/3/chapter/1/page/0?format=image%2Fjpeg"). A path that already is a full http(s)
// URL comes back unchanged; an empty path gives "".
std::string absoluteUrl(std::string_view baseUrl, std::string_view path);

// True if url is on the server: it starts with the base URL, followed by "/", "?" or nothing.
// "http://h:4567/api" is on "http://h:4567"; "http://h:45678/api" is not.
bool isServerUrl(std::string_view baseUrl, std::string_view url);

}  // namespace snx::api
