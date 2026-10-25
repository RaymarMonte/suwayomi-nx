# Vendored single headers (versions and checksums in README.md). Header-only, so these are INTERFACE targets.

# #include <nlohmann/json.hpp>
# Not SYSTEM on purpose: GCC searches -isystem folders after all -I folders, and then borealis's older copy (-I) would win (D26).
add_library(third_party_json INTERFACE)
target_include_directories(third_party_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# #include <doctest/doctest.h>
add_library(third_party_doctest INTERFACE)
target_include_directories(third_party_doctest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
