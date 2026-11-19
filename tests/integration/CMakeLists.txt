# Integration tests (TESTING.md layer 3, WP-06): the api module (and from WP-07 the image loader) against the live Suwayomi
# test server (tools/test-server).
# Not a ctest entry: it needs the server. Run them with tools/build.sh integration (all three auth modes).
add_executable(snx_integration_tests main.cpp test_suwayomi_client.cpp)
# WP-18: snx_platform + snx_ui_models for the Switch downloader (SafeFs into a temp folder).
target_link_libraries(snx_integration_tests PRIVATE snx_core snx_net snx_api snx_image snx_platform snx_ui_models
    third_party_doctest)
target_include_directories(snx_integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit)  # ApiTestKit.hpp
target_compile_definitions(snx_integration_tests PRIVATE SNX_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
    SNX_TEST_SITE_DIR="${PROJECT_SOURCE_DIR}/.test-server/web/site")  # the test source's switch file (WP-14)
snx_target_defaults(snx_integration_tests)
