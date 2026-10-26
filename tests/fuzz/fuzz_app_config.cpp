// Fuzz target (SAFETY.md S6, WP-04): any bytes as config.json. Loading must never crash, and whatever it loads must save and
// load back unchanged with no problems (so a damaged file can't turn into a config that breaks the next start).
// Seeds: tests/fuzz/seeds/app-config/. Run: tools/build.sh fuzz
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/AppConfig.hpp"

using namespace snx::core;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const ConfigLoad load = loadAppConfig(text);
    const std::string saved = saveAppConfig(load.config);
    const ConfigLoad again = loadAppConfig(saved);
    if (!again.problems.empty() || saveAppConfig(again.config) != saved) {
        std::abort();  // a round trip that changes the settings is a bug
    }
    return 0;
}
