#pragma once

#include <functional>

#include "api/SuwayomiClient.hpp"
#include "net/CancelToken.hpp"

namespace snx::ui {

// The connection check the main and setup screens run (WP-08): the server's version (aboutServer), and with a login also the
// categories. Suwayomi answers aboutServer without a login even in ui_login mode (SUWAYOMI-API.md), so only the second call
// proves the login works (and makes the client renew or log in, D38). `done` gets the version or the first error, on the main
// thread (the client's deliver), unless `cancel` fired: then it isn't called at all.
void checkServer(api::SuwayomiClient& client, const net::CancelToken& cancel,
                 std::function<void(const api::ApiResult<core::ServerInfo>&)> done);

}  // namespace snx::ui
