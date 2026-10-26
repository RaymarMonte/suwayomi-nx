#include "ui/components/ServerCheck.hpp"

#include <utility>
#include <vector>

namespace snx::ui {

void checkServer(api::SuwayomiClient& client, const net::CancelToken& cancel,
                 std::function<void(const api::ApiResult<core::ServerInfo>&)> done) {
    // `client` outlives every screen (AppContext), so the second call may use it from the first one's callback.
    client.serverInfo(cancel).then([&client, cancel, done = std::move(done)](const api::ApiResult<core::ServerInfo>& info) {
        if (cancel.cancelled()) {
            return;
        }
        if (!info.ok() || client.settings().auth == core::AuthMode::None) {
            done(info);
            return;
        }
        client.categories(cancel).then(
            [cancel, info, done](const api::ApiResult<std::vector<core::Category>>& categories) {
                if (cancel.cancelled()) {
                    return;
                }
                done(categories.ok() ? info : api::ApiResult<core::ServerInfo>(categories.error()));
            });
    });
}

}  // namespace snx::ui
