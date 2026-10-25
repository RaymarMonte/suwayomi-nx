# core: pure C++ (models, JSON mapping, GraphQL docs, reader math, policies). Never curl, borealis, libnx or platform headers.
add_library(snx_core STATIC
    CrashRecord.cpp
    LaunchOptions.cpp
    RuntimeInfo.cpp
    SafePath.cpp
    TestConfig.cpp
    TestEvents.cpp
    Version.cpp
)
target_include_directories(snx_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(snx_core PRIVATE SNX_VERSION="${PROJECT_VERSION}")
target_link_libraries(snx_core PUBLIC third_party_json)
snx_target_defaults(snx_core)
