#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "net/HttpClient.hpp"

// Small helpers for the api tests: look inside a GraphQL request the client sent.

// The operation a GraphQL POST body carries ("Login", "Categories"), read from its document; "" if it isn't one.
inline std::string operationOf(std::string_view body) {
    const nlohmann::json doc = nlohmann::json::parse(body, nullptr, false);
    if (!doc.is_object() || !doc.contains("query") || !doc["query"].is_string()) {
        return {};
    }
    const std::string query = doc["query"].get<std::string>();
    std::size_t start = std::string::npos;
    for (const std::string_view keyword : {"query ", "mutation "}) {
        const std::size_t at = query.find(keyword);
        if (at != std::string::npos && (start == std::string::npos || at < start)) {
            start = at + keyword.size();
        }
    }
    if (start == std::string::npos) {
        return {};
    }
    return query.substr(start, query.find_first_of(" ({\n", start) - start);
}

inline std::string operationOf(const snx::net::Request& request) {
    return operationOf(request.body);
}

// The request's GraphQL variables ({} if none).
inline nlohmann::json variablesOf(const snx::net::Request& request) {
    const nlohmann::json doc = nlohmann::json::parse(request.body, nullptr, false);
    return doc.is_object() && doc.contains("variables") ? doc["variables"] : nlohmann::json::object();
}

// The value of a request header ("" if it has none).
inline std::string headerOf(const snx::net::Request& request, std::string_view name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}
