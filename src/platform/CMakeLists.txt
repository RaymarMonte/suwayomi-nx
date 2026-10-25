# platform: one interface (Platform.hpp), two implementations. Only the one for this build gets compiled.
# libnx is only ever included from platform/switch/ (CLAUDE.md "Rules for code").
# SafeFs (the write jail) is shared: both platforms have the same POSIX file functions.
if (PLATFORM_SWITCH)
    add_library(snx_platform STATIC SafeFs.cpp switch/SwitchPlatform.cpp)
else ()
    add_library(snx_platform STATIC SafeFs.cpp desktop/DesktopPlatform.cpp)
endif ()
target_link_libraries(snx_platform PUBLIC snx_core)
snx_target_defaults(snx_platform)
