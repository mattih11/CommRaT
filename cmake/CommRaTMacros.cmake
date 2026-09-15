# CommRaT CMake helper macros

# commrat_module(TARGET
#     SOURCES  source1.cpp [source2.cpp ...]
#     MODULE_CLASS  ClassName
#     [LINKS  lib1 lib2 ...]
# )
#
# Creates an executable for a CommRaT module binary and generates a
# descriptor file: <ModuleClass>.module.json
#
# Native builds run the binary with --commrat-inspect after linking. The build
# fails if inspection does not produce a complete, parseable descriptor.
# EVL cross-builds retain an explicit placeholder until the evl-descriptors
# target runs inspection inside QEMU.
#
# ProcessLauncher auto-discovers *.module.json files in dirname(argv[0]) at
# runtime to map module_class names to binary paths in AppDescription configs.
#
# Example:
#   commrat_module(my_sensor
#       SOURCES  my_sensor.cpp
#       MODULE_CLASS  MySensorModule)
function(commrat_module TARGET)
    cmake_parse_arguments(ARG "" "MODULE_CLASS" "SOURCES;LINKS" ${ARGN})

    if(NOT ARG_MODULE_CLASS)
        message(FATAL_ERROR "commrat_module(${TARGET}): MODULE_CLASS is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "commrat_module(${TARGET}): SOURCES is required")
    endif()

    add_executable(${TARGET} ${ARG_SOURCES})
    target_link_libraries(${TARGET} PRIVATE commrat ${ARG_LINKS})

    # Track EVL module targets so commrat_finalize_evl_descriptors() can
    # wire them as dependencies of the evl-descriptors target.
    if(DEFINED _EVL_SDK)
        set_property(GLOBAL APPEND PROPERTY _COMMRAT_EVL_TARGETS ${TARGET})
    endif()

    # EVL toolchain doesn't set CMAKE_CROSSCOMPILING (same arch, amd64→amd64),
    # so use _EVL_SDK as the discriminator: defined only when the ISAR SDK
    # toolchain is active and the binary links against libevl from the sysroot.
    # EVL binaries cannot run on the host: libevl constructors require an EVL
    # kernel and crash immediately, even for --commrat-inspect. The cmake-
    # generate-time placeholder (module_class + binary) is sufficient for EVL
    # deployments; the STD build produces full descriptors for tooling/GUI use.
    set(_descriptor "${CMAKE_CURRENT_BINARY_DIR}/${ARG_MODULE_CLASS}.module.json")
    if(DEFINED _EVL_SDK)
        file(GENERATE
            OUTPUT "${_descriptor}"
            CONTENT "{\n  \"module_class\": \"${ARG_MODULE_CLASS}\",\n  \"binary\": \"$<TARGET_FILE:${TARGET}>\"\n}\n")
    else()
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DCOMMRAT_INSPECT_EXECUTABLE=$<TARGET_FILE:${TARGET}>"
                "-DCOMMRAT_DESCRIPTOR=${_descriptor}"
                "-DCOMMRAT_MODULE_CLASS=${ARG_MODULE_CLASS}"
                "-DCOMMRAT_MODULE_BINARY=$<TARGET_FILE:${TARGET}>"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunModuleInspection.cmake"
            COMMENT "Generating descriptor for ${ARG_MODULE_CLASS}"
            VERBATIM
        )
    endif()
endfunction()

# commrat_finalize_evl_descriptors()
#
# Creates an `evl-descriptors` cmake target (only when _EVL_SDK is defined)
# that depends on all EVL module binaries and runs them inside QEMU via
#   scripts/evl-dev.sh --generate-descriptors <preset>
# to produce full descriptors in a single QEMU session, then syncs them back.
#
# Call once from the top-level CMakeLists.txt after all add_subdirectory() calls.
function(commrat_finalize_evl_descriptors)
    if(NOT DEFINED _EVL_SDK)
        return()
    endif()
    get_property(_targets GLOBAL PROPERTY _COMMRAT_EVL_TARGETS)
    if(NOT _targets)
        return()
    endif()

    # Derive the cmake preset name from the build directory (e.g. build/evl-cross → evl-cross).
    get_filename_component(_preset "${CMAKE_BINARY_DIR}" NAME)

    add_custom_target(evl-descriptors
        DEPENDS ${_targets}
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/evl-dev.sh
                --generate-descriptors "${_preset}"
        COMMENT "Generating EVL module descriptors via QEMU (single session)"
        VERBATIM
    )
endfunction()
