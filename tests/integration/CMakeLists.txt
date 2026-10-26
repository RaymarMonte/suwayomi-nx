# Integration tests (TESTING.md layer 3, WP-06): the api module against the live Suwayomi test server (tools/test-server).
# Not a ctest entry: it needs the server. Run them with tools/build.sh integration (all three auth modes).
add_executable(snx_integration_tests main.cpp test_suwayomi_client.cpp)
target_link_libraries(snx_integration_tests PRIVATE snx_core snx_net snx_api third_party_doctest)
target_include_directories(snx_integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit)  # ApiTestKit.hpp
target_compile_definitions(snx_integration_tests PRIVATE SNX_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
snx_target_defaults(snx_integration_tests)
