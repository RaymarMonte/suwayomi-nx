# The app: main() and wiring (src/app/) plus the borealis screens (src/ui/). Only built with PLATFORM_DESKTOP or PLATFORM_SWITCH.
# Outputs land in the top of the build folder:
#   desktop: suwayomi-nx + resources/          (run it from that folder: borealis looks for ./resources/)
#   switch:  suwayomi-nx.elf (with debug info), suwayomi-nx.map, suwayomi-nx.nacp, suwayomi-nx.nro (target "suwayomi-nx.nro")
set(SNX_APP suwayomi-nx)
set(SNX_APP_TITLE "Suwayomi NX")
set(SNX_APP_AUTHOR "suwayomi-nx contributors")
set(SNX_BOREALIS_RESOURCES ${PROJECT_SOURCE_DIR}/library/borealis/resources)

set(SNX_APP_SOURCES
    main.cpp
    AppletGuard.cpp
    ConfigStore.cpp
    Logger.cpp
    TestHooks.cpp
    crash/Crash.cpp
    crash/Drills.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/crash/CrashActivity.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/components/CoverGrid.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/components/CoverImage.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/components/ServerCheck.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/components/StatusView.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/components/TextFit.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/diagnostics/DiagnosticsActivity.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/library/LibraryTab.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/login/LoginActivity.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/main/MainActivity.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/main/SettingsTab.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/manga/MangaActivity.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/reader/ReaderActivity.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/setup/SetupActivity.cpp
)
if (PLATFORM_SWITCH)
    # crash/CrashSwitch.cpp defines libnx's crash hooks and SwitchStandIns.cpp the D28 stand-ins. Both must be in the
    # executable's own objects, not in a library (crash/README.md says why).
    # switch_wrapper.c: borealis's userAppInit/userAppExit (sockets, romfs and the system services it uses, as in borealis_template).
    list(APPEND SNX_APP_SOURCES crash/CrashSwitch.cpp SwitchStandIns.cpp ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
else ()
    list(APPEND SNX_APP_SOURCES crash/CrashDesktop.cpp)
endif ()

# tools/safety/gate-selftest.sh only: S2, S3, BOOST or BOOSTPTR links one banned call on purpose, so the safety gate must
# fail the build (src/app/SafetyGateProbe.cpp).
set(SNX_SAFETY_GATE_PROBE "" CACHE STRING
    "Safety gate self-test only: S2, S3, BOOST or BOOSTPTR links a banned call, the build must then fail")
if (PLATFORM_SWITCH AND SNX_SAFETY_GATE_PROBE)
    list(APPEND SNX_APP_SOURCES SafetyGateProbe.cpp)
endif ()

find_package(Threads REQUIRED)
program_target(${SNX_APP} "${SNX_APP_SOURCES}")
set_target_properties(${SNX_APP} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(${SNX_APP} PRIVATE snx_ui_models snx_api snx_image snx_platform borealis Threads::Threads)
snx_target_defaults(${SNX_APP})
# Our nlohmann/json goes before borealis's older copy (D26).
target_include_directories(${SNX_APP} BEFORE PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
# borealis's headers count as system headers here: their warnings under our -Wextra are upstream's, not ours, and
# GCC searches system folders after -I folders, which keeps borealis's nlohmann/json behind ours as well.
get_target_property(SNX_BOREALIS_INCLUDES borealis INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(${SNX_APP} SYSTEM PRIVATE ${SNX_BOREALIS_INCLUDES})

# Resources, put together at build time: our resources/ on top of the parts of borealis's resources it needs at runtime.
# borealis's files come from the submodule, so its fonts and icons are never copied into our repo.
#   material/  icon font (Apache-2.0)      img/sys/  battery, Wi-Fi and cursor images
#   i18n/<lang>/hints.json  borealis's own texts ("hints/exit", "hints/back", ...). Its other i18n files belong to its demo.
#   font/      desktop only: on the Switch borealis uses the system font
#   shaders/   Switch only: deko3d shaders compiled by uam
if (PLATFORM_SWITCH)
    set(SNX_RESOURCES_OUT ${CMAKE_BINARY_DIR}/romfs)
else ()
    set(SNX_RESOURCES_OUT ${CMAKE_BINARY_DIR}/resources)
endif ()
set(SNX_RESOURCE_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${SNX_RESOURCES_OUT}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SNX_BOREALIS_RESOURCES}/material ${SNX_RESOURCES_OUT}/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SNX_BOREALIS_RESOURCES}/img/sys ${SNX_RESOURCES_OUT}/img/sys
)
file(GLOB SNX_BOREALIS_HINTS RELATIVE ${SNX_BOREALIS_RESOURCES}/i18n ${SNX_BOREALIS_RESOURCES}/i18n/*/hints.json)
foreach (hints ${SNX_BOREALIS_HINTS})
    get_filename_component(lang ${hints} DIRECTORY)
    list(APPEND SNX_RESOURCE_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SNX_RESOURCES_OUT}/i18n/${lang}
        COMMAND ${CMAKE_COMMAND} -E copy ${SNX_BOREALIS_RESOURCES}/i18n/${hints} ${SNX_RESOURCES_OUT}/i18n/${lang}/hints.json)
endforeach ()
if (PLATFORM_DESKTOP)
    list(APPEND SNX_RESOURCE_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${SNX_BOREALIS_RESOURCES}/font ${SNX_RESOURCES_OUT}/font)
endif ()
list(APPEND SNX_RESOURCE_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/resources ${SNX_RESOURCES_OUT})
if (PLATFORM_SWITCH AND USE_DEKO3D)
    # borealis's gen_dksh compiles the shaders at configure time, into the folder we give it (the build folder, not the source).
    gen_dksh(${CMAKE_BINARY_DIR}/shaders)
    list(APPEND SNX_RESOURCE_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/shaders ${SNX_RESOURCES_OUT}/shaders)
endif ()
add_custom_target(snx_resources ${SNX_RESOURCE_COMMANDS} COMMENT "Collecting resources into ${SNX_RESOURCES_OUT}")

if (PLATFORM_DESKTOP)
    add_dependencies(${SNX_APP} snx_resources)
elseif (PLATFORM_SWITCH)
    # The linker map says which object pulled in each library function. tools/safety/check-elf.sh uses it.
    target_link_options(${SNX_APP} PRIVATE "-Wl,-Map,${CMAKE_BINARY_DIR}/${SNX_APP}.map")
    # D28: every call to a function in stand-ins.txt goes to our do-nothing __wrap_<name> (SwitchStandIns.cpp) instead.
    set(SNX_STAND_INS_FILE ${PROJECT_SOURCE_DIR}/tools/safety/stand-ins.txt)
    file(STRINGS ${SNX_STAND_INS_FILE} SNX_STAND_INS REGEX "^[A-Za-z_][A-Za-z0-9_]*$")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SNX_STAND_INS_FILE})
    foreach (name ${SNX_STAND_INS})
        target_link_options(${SNX_APP} PRIVATE "-Wl,--wrap=${name}")
    endforeach ()
    if (SNX_SAFETY_GATE_PROBE)
        # --undefined keeps the probe function although nothing calls it (it is a root for --gc-sections).
        target_compile_definitions(${SNX_APP} PRIVATE SNX_SAFETY_GATE_PROBE_${SNX_SAFETY_GATE_PROBE}=1)
        target_link_options(${SNX_APP} PRIVATE "-Wl,--undefined=snxSafetyGateProbe")
    endif ()
    add_custom_target(${SNX_APP}.nro ALL
        DEPENDS ${SNX_APP} snx_resources
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        # The safety gate (SAFETY.md S2/S3, D28) comes first: no .nro at all unless the .elf passes. Report: safety-report.txt.
        COMMAND ${CMAKE_COMMAND} -E rm -f ${SNX_APP}.nro
        COMMAND ${PROJECT_SOURCE_DIR}/tools/safety/check-elf.sh $<TARGET_FILE:${SNX_APP}> ${SNX_APP}.map --report safety-report.txt
        COMMAND ${NX_NACPTOOL_EXE} --create "${SNX_APP_TITLE}" "${SNX_APP_AUTHOR}" "${PROJECT_VERSION}" ${SNX_APP}.nacp
        COMMAND ${NX_ELF2NRO_EXE} $<TARGET_FILE:${SNX_APP}> ${SNX_APP}.nro
                --icon=${PROJECT_SOURCE_DIR}/resources/icon/icon.jpg --nacp=${SNX_APP}.nacp --romfsdir=${SNX_RESOURCES_OUT}
        COMMENT "Packing ${SNX_APP}.nro"
    )
endif ()
