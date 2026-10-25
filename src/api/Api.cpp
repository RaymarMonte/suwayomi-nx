#include "api/Api.hpp"

#include "core/Version.hpp"

namespace snx::api {

std::string userAgent() {
    return "suwayomi-nx/" + std::string(core::appVersion());
}

}  // namespace snx::api
