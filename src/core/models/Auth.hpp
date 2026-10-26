#pragma once

#include <string>

namespace snx::core {

// ui_login: what login returns. refreshToken returns only a new access token.
struct LoginTokens {
    std::string accessToken;   // sent as "Authorization: Bearer <access>", lives 5 minutes by default
    std::string refreshToken;  // gets a new access token, lives 60 days by default
};

}  // namespace snx::core
