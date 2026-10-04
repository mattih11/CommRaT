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
# A private, build-only inspector is compiled from the module sources and writes
# the complete descriptor after the runtime binary links. The runtime executable
# does not contain descriptor serialization or the --commrat-inspect entry point.
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

    set(_inspector "${TARGET}__commrat_descriptor_inspector")
    add_executable(${_inspector} EXCLUDE_FROM_ALL ${ARG_SOURCES})
    target_compile_definitions(${_inspector} PRIVATE COMMRAT_DESCRIPTOR_INSPECTOR=1)
    target_link_libraries(${_inspector} PRIVATE commrat ${ARG_LINKS})

    target_include_directories(${_inspector} PRIVATE
        "$<TARGET_PROPERTY:${TARGET},INCLUDE_DIRECTORIES>")
    target_compile_definitions(${_inspector} PRIVATE
        "$<TARGET_PROPERTY:${TARGET},COMPILE_DEFINITIONS>")
    target_compile_options(${_inspector} PRIVATE
        "$<TARGET_PROPERTY:${TARGET},COMPILE_OPTIONS>")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND UNIX)
        target_link_options(${_inspector} PRIVATE "LINKER:--as-needed")
    endif()

    if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
        message(FATAL_ERROR
            "commrat_module(${TARGET}): descriptor generation requires "
            "CMAKE_CROSSCOMPILING_EMULATOR when the inspector cannot run on the host")
    endif()

    add_dependencies(${TARGET} ${_inspector})
    set(_descriptor "${CMAKE_CURRENT_BINARY_DIR}/${ARG_MODULE_CLASS}.module.json")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DCOMMRAT_INSPECT_EXECUTABLE=$<TARGET_FILE:${_inspector}>"
            "-DCOMMRAT_INSPECT_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
            "-DCOMMRAT_DESCRIPTOR=${_descriptor}"
            "-DCOMMRAT_MODULE_CLASS=${ARG_MODULE_CLASS}"
            "-DCOMMRAT_MODULE_BINARY=$<TARGET_FILE:${TARGET}>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunModuleInspection.cmake"
        COMMENT "Generating descriptor for ${ARG_MODULE_CLASS}"
        VERBATIM
    )
endfunction()
