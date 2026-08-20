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
# Two-stage generation:
#   1. cmake generate time: placeholder with {module_class, binary} only.
#   2. POST_BUILD: binary is run with --commrat-inspect to overwrite the
#      placeholder with the full schema (outputs, inputs, execution_mode, etc.)
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

    # Placeholder at cmake generate time so the file exists before any build.
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${ARG_MODULE_CLASS}.module.json"
        CONTENT "{\n  \"module_class\": \"${ARG_MODULE_CLASS}\",\n  \"binary\": \"$<TARGET_FILE:${TARGET}>\"\n}\n"
    )

    # Overwrite with full schema (I/O types, execution mode, period) after each build.
    # argv: --commrat-inspect <outfile> <module_class> <binary>
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND $<TARGET_FILE:${TARGET}>
                --commrat-inspect
                "${CMAKE_CURRENT_BINARY_DIR}/${ARG_MODULE_CLASS}.module.json"
                "${ARG_MODULE_CLASS}"
                "$<TARGET_FILE:${TARGET}>"
        COMMENT "Generating descriptor for ${ARG_MODULE_CLASS}"
        VERBATIM
    )
endfunction()
