# Host unit tests: one doctest binary, one ctest entry per module (its doctest suite), so a failure names the module.
set(SNX_TEST_MODULES core net api image platform ui)

set(SNX_TEST_SOURCES main.cpp)
foreach (module ${SNX_TEST_MODULES})
    list(APPEND SNX_TEST_SOURCES test_${module}.cpp)
endforeach ()
# More files for a module's suite: the safety layer's tests (WP-03b), models/GraphQL/config/profile (WP-04), curl (WP-05),
# SuwayomiClient (WP-06), image math and the image loader (WP-07), log buffer, save timing, test config and events (WP-08),
# the library model and grid (WP-09), the manga model (WP-10), the reader math and model (WP-11).
list(APPEND SNX_TEST_SOURCES test_core_safety.cpp test_platform_safefs.cpp
    test_core_json.cpp test_core_models.cpp test_core_graphql.cpp test_core_config.cpp test_core_profile.cpp
    test_net_curl.cpp test_api_client.cpp test_api_curl.cpp test_core_image.cpp test_image_loader.cpp test_core_app.cpp
    test_ui_library.cpp
    test_ui_manga.cpp
    test_core_reader.cpp test_ui_reader.cpp)

add_executable(snx_unit_tests ${SNX_TEST_SOURCES})
target_link_libraries(snx_unit_tests PRIVATE snx_core snx_net snx_api snx_image snx_platform snx_ui_models third_party_doctest third_party_httplib)
target_compile_definitions(snx_unit_tests PRIVATE SNX_TEST_EXPECTED_VERSION="${PROJECT_VERSION}"
    SNX_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
snx_target_defaults(snx_unit_tests)

foreach (module ${SNX_TEST_MODULES})
    add_test(NAME unit.${module} COMMAND snx_unit_tests --test-suite=${module})
    # A suite name that matches nothing would "pass" with 0 tests. Count that as a failure.
    set_tests_properties(unit.${module} PROPERTIES FAIL_REGULAR_EXPRESSION "test cases: +0 [|]")
endforeach ()

# core must stay free of curl/borealis/libnx/platform headers (WP-04).
add_test(NAME unit.core-includes COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${PROJECT_SOURCE_DIR}/src/core
    -P ${PROJECT_SOURCE_DIR}/src/core/CheckIncludes.cmake)
