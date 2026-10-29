# core: pure C++ (models, JSON mapping, GraphQL docs, reader math, policies). Never curl, borealis, libnx or platform headers
# (checked by the ctest entry unit.core-includes, CheckIncludes.cmake).

# The GraphQL documents become string constants at build time (D4, WP-04): graphql/EmbedGraphql.cmake writes
# GraphqlDocuments.inc into the build folder whenever a .graphql file changes. A new .graphql file re-runs CMake (CONFIGURE_DEPENDS).
file(GLOB SNX_GRAPHQL_DOCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/graphql/*.graphql)
set(SNX_GRAPHQL_INC ${CMAKE_CURRENT_BINARY_DIR}/generated/GraphqlDocuments.inc)
add_custom_command(
    OUTPUT ${SNX_GRAPHQL_INC}
    COMMAND ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/graphql -DOUT=${SNX_GRAPHQL_INC}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/graphql/EmbedGraphql.cmake
    DEPENDS ${SNX_GRAPHQL_DOCS} ${CMAKE_CURRENT_SOURCE_DIR}/graphql/EmbedGraphql.cmake
    COMMENT "Embedding src/core/graphql/*.graphql"
    VERBATIM
)

add_library(snx_core STATIC
    AppConfig.cpp
    CrashRecord.cpp
    ImageGeometry.cpp
    Json.cpp
    LaunchOptions.cpp
    LogBuffer.cpp
    MemoryPressure.cpp
    ProgressSync.cpp
    ReaderMath.cpp
    RuntimeInfo.cpp
    RetryPolicy.cpp
    RuntimeProfile.cpp
    SafePath.cpp
    SaveScheduler.cpp
    TestConfig.cpp
    TestEvents.cpp
    Version.cpp
    graphql/Documents.cpp
    graphql/Gql.cpp
    graphql/Operations.cpp
    models/FromJson.cpp
    models/Models.cpp
    ${SNX_GRAPHQL_INC}
)
target_include_directories(snx_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_include_directories(snx_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(snx_core PRIVATE SNX_VERSION="${PROJECT_VERSION}")
target_link_libraries(snx_core PUBLIC third_party_json)
snx_target_defaults(snx_core)
