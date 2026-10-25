cmake_minimum_required(VERSION 3.16)

# suwayomi-nx: one CMake project, three kinds of build (D2, WP-01). tools/build.sh wraps them.
#   -DPLATFORM_DESKTOP=ON    desktop app for Linux/WSLg (borealis with GLFW + OpenGL 3)
#   -DPLATFORM_SWITCH=ON     Switch app: target suwayomi-nx.nro builds suwayomi-nx.elf + suwayomi-nx.nro (deko3d, D2)
#   neither of them          host unit tests only (-DSNX_BUILD_TESTS=ON): no borealis, builds in seconds
# The borealis part follows xfangfang/borealis_template (Apache-2.0, see CREDITS.md).

option(SNX_BUILD_TESTS "Build the host unit tests (tests/unit)" OFF)
option(SNX_TEST_HOOKS "Test build: the app writes test events (docs/TESTING.md)" OFF)
option(SNX_COVERAGE "Measure line coverage of the unit tests (gcov, host only)" OFF)

if (PLATFORM_DESKTOP AND PLATFORM_SWITCH)
    message(FATAL_ERROR "Pick one: PLATFORM_DESKTOP or PLATFORM_SWITCH")
endif ()
if (PLATFORM_SWITCH AND (SNX_BUILD_TESTS OR SNX_COVERAGE))
    message(FATAL_ERROR "Unit tests run on the PC only. Build them without PLATFORM_SWITCH")
endif ()
set(SNX_BUILD_APP OFF)
if (PLATFORM_DESKTOP OR PLATFORM_SWITCH)
    set(SNX_BUILD_APP ON)
elseif (NOT SNX_BUILD_TESTS)
    message(FATAL_ERROR "Nothing to build. Use tools/build.sh, or -DPLATFORM_DESKTOP=ON, -DPLATFORM_SWITCH=ON or -DSNX_BUILD_TESTS=ON")
endif ()

if (SNX_BUILD_APP)
    set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis/library)
    if (NOT EXISTS ${BOREALIS_LIBRARY}/CMakeLists.txt OR NOT EXISTS ${BOREALIS_LIBRARY}/lib/extern/glfw/CMakeLists.txt)
        message(FATAL_ERROR "borealis is missing. Run: git submodule update --init library/borealis && "
                "git -C library/borealis submodule update --init library/lib/extern/glfw")
    endif ()
    # deko3d is our Switch graphics backend (D2). borealis's own default is OpenGL.
    if (PLATFORM_SWITCH AND NOT DEFINED USE_DEKO3D)
        set(USE_DEKO3D ON CACHE BOOL "Use deko3d instead of OpenGL")
    endif ()
    include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
    # borealis brings its own copies of these libraries. Use them, not the system's.
    option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
    cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
    cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
    cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)
    # Picks the compilers (devkitA64 for the Switch), so it has to come before project().
    include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)
endif ()

project(suwayomi-nx VERSION 0.0.1 LANGUAGES C CXX)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif ()

if (PLATFORM_SWITCH)
    set(SNX_PLATFORM switch)
else ()
    set(SNX_PLATFORM desktop)
endif ()

# Debug info everywhere, so crash logs can be symbolized (WP-01). The Switch toolchain already adds -g;
# borealis's desktop Release flags don't. elf2nro leaves debug info out of the .nro, the .elf keeps it.
add_compile_options(-g)

# Settings for our own targets (borealis keeps its own): C++17 with exceptions and RTTI (D3), warnings on.
function(snx_target_defaults target)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(${target} PRIVATE -Wall -Wextra -ffunction-sections -fdata-sections)
    if (PLATFORM_SWITCH)
        # Frame records in every function of ours, so the crash handler can walk the stack (src/app/crash/README.md).
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer)
    endif ()
    if (SNX_TEST_HOOKS)
        target_compile_definitions(${target} PUBLIC SNX_TEST_HOOKS=1)
    endif ()
    if (SNX_COVERAGE)
        target_compile_options(${target} PRIVATE --coverage -O0)
        target_link_options(${target} PUBLIC --coverage)
    endif ()
endfunction()

add_subdirectory(third_party)
add_subdirectory(src/core)
add_subdirectory(src/net)
add_subdirectory(src/api)
add_subdirectory(src/image)
add_subdirectory(src/platform)

if (SNX_BUILD_APP)
    add_subdirectory(${BOREALIS_LIBRARY} borealis)
    add_subdirectory(src/app)
endif ()

if (SNX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif ()
