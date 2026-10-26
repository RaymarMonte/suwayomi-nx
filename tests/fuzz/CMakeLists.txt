# Fuzz targets (SAFETY.md S6, WP-04), built by tools/build.sh fuzz with -DSNX_FUZZ=ON (top-level CMakeLists.txt adds the
# sanitizer flags to every target). With clang: libFuzzer targets. With GCC: the same targets linked to ReplayMain.cpp, which
# only replays the seed corpus (GCC has no libFuzzer).
set(SNX_FUZZ_TARGETS fuzz_gql_response fuzz_app_config fuzz_image_decode)

foreach (target ${SNX_FUZZ_TARGETS})
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_executable(${target} ${target}.cpp)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    else ()
        add_executable(${target} ${target}.cpp ReplayMain.cpp)
    endif ()
    target_link_libraries(${target} PRIVATE snx_core)
    snx_target_defaults(${target})
endforeach ()
# WP-07: sniff + every image decoder (libwebp is built from source here, so it's instrumented too).
target_link_libraries(fuzz_image_decode PRIVATE snx_image)
add_custom_target(snx_fuzz DEPENDS ${SNX_FUZZ_TARGETS})
