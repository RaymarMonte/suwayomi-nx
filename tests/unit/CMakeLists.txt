# Host unit tests: one doctest binary, one ctest entry per module (its doctest suite), so a failure names the module.
set(SNX_TEST_MODULES core net api image platform)

set(SNX_TEST_SOURCES main.cpp)
foreach (module ${SNX_TEST_MODULES})
    list(APPEND SNX_TEST_SOURCES test_${module}.cpp)
endforeach ()
# More files for a module's suite: the safety layer's tests (WP-03b).
list(APPEND SNX_TEST_SOURCES test_core_safety.cpp test_platform_safefs.cpp)

add_executable(snx_unit_tests ${SNX_TEST_SOURCES})
target_link_libraries(snx_unit_tests PRIVATE snx_core snx_net snx_api snx_image snx_platform third_party_doctest)
target_compile_definitions(snx_unit_tests PRIVATE SNX_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")
snx_target_defaults(snx_unit_tests)

foreach (module ${SNX_TEST_MODULES})
    add_test(NAME unit.${module} COMMAND snx_unit_tests --test-suite=${module})
    # A suite name that matches nothing would "pass" with 0 tests. Count that as a failure.
    set_tests_properties(unit.${module} PROPERTIES FAIL_REGULAR_EXPRESSION "test cases: +0 [|]")
endforeach ()
