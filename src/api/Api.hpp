#pragma once

#include <string>

namespace snx::api {

// The User-Agent the app sends to Suwayomi-Server, e.g. "suwayomi-nx/0.0.1".
// Placeholder from WP-01; WP-06 builds SuwayomiClient around it.
std::string userAgent();

}  // namespace snx::api
