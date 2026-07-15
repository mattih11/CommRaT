# CommRaT CMake helper macros

# commrat_module(TARGET
#     SOURCES  source1.cpp [source2.cpp ...]
#     MODULE_CLASS  ClassName
#     [LINKS  lib1 lib2 ...]
# )
#
# Creates an executable for a CommRaT module binary and generates a
# cmake-managed descriptor file: <ModuleClass>.module.json
#
# The descriptor is placed in CMAKE_CURRENT_BINARY_DIR alongside the binary
# and contains:
#   {
#     "module_class": "ClassName",
#     "binary": "/absolute/path/to/TARGET"
#   }
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

    # Generate the descriptor JSON at configure/generate time.
    # $<TARGET_FILE:TARGET> resolves to the full path of the binary, respecting
    # multi-config generators (Debug/Release/etc.) automatically.
    file(GENERATE
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${ARG_MODULE_CLASS}.module.json"
        CONTENT "{\n  \"module_class\": \"${ARG_MODULE_CLASS}\",\n  \"binary\": \"$<TARGET_FILE:${TARGET}>\"\n}\n"
    )
endfunction()
