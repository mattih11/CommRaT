# CommRaTSchemaGen.cmake
# ----------------------
# Provides commrat_generate_schema() — a CMake helper that compiles the
# CommRaT schema-gen driver against a user-supplied registry header and
# runs it as a post-build step to produce a CommRaTSchemaOutput JSON file.
#
# The output JSON contains the full CommRaT metadata (message IDs, registry
# name, type names) plus the sertial StructLayout for every message type
# registered in the CommRaT<> app registry.  It is readable by both
# sertial-inspect (layout field only) and commrat-inspect (both blocks).
#
# Usage
# -----
#   find_package(CommRaT REQUIRED)       # loads CommRaTSchemaGen automatically
#
#   commrat_generate_schema(
#       TARGET          my_module        # existing CMake target to attach to
#       APP_HEADER      /abs/path/to/my_app.hpp  # defines the CommRaT<> alias
#       APP_TYPE        MyApp            # C++ name of the CommRaT<> alias
#       OUTPUT          ${CMAKE_CURRENT_BINARY_DIR}/my_app_schema.json
#   )
#
# Arguments
# ---------
#   TARGET (required)
#       An existing CMake target.  The schema generator is built as
#       <TARGET>_schema_gen and the post-build command is attached to TARGET.
#       The generator inherits the include directories of TARGET so that
#       APP_HEADER (and headers it includes) can be found.
#
#   APP_HEADER (required)
#       Absolute path to the header that contains the CommRaT<> registry alias,
#       e.g.:
#           include/commrat/examples/all_examples_app.hpp
#       The header must be visible via TARGET's include directories or be an
#       absolute path.
#
#   APP_TYPE (required)
#       C++ type name of the CommRaT<> registry alias defined in APP_HEADER.
#       Examples:
#           AllExamplesApp
#           my_namespace::MyApp
#       If the alias is at global scope with no namespace, just the bare name.
#
#   OUTPUT (required)
#       Absolute path for the generated JSON schema file.  The file is (re)written
#       every time TARGET is rebuilt.  Recommended convention:
#           ${CMAKE_CURRENT_BINARY_DIR}/<app_name>_schema.json
#
# Example — generate a schema for AllExamplesApp alongside the examples:
#
#   commrat_generate_schema(
#       TARGET     example_gui_producer
#       APP_HEADER "${CMAKE_SOURCE_DIR}/include/commrat/examples/all_examples_app.hpp"
#       APP_TYPE   AllExamplesApp
#       OUTPUT     "${CMAKE_BINARY_DIR}/all_examples_schema.json"
#   )
#
# The generated file can be opened directly in tools/commrat-inspect/viewer.html
# (File > Open) or served via a local HTTP server and loaded via ?schema=<url>.

include_guard(GLOBAL)

# Capture this module's directory at include time — CMAKE_CURRENT_LIST_DIR
# inside a function body resolves to the *caller's* directory.
set(_COMMRAT_SCHEMA_GEN_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

function(commrat_generate_schema)
    cmake_parse_arguments(
        CGS                              # prefix
        ""                               # options
        "TARGET;APP_HEADER;APP_TYPE;OUTPUT"
        ""                               # multi-value
        ${ARGN}
    )

    # ---- Validate required arguments ----------------------------------------
    foreach(req TARGET APP_HEADER APP_TYPE OUTPUT)
        if(NOT DEFINED CGS_${req})
            message(FATAL_ERROR "commrat_generate_schema: ${req} is required")
        endif()
    endforeach()

    if(NOT TARGET ${CGS_TARGET})
        message(FATAL_ERROR
            "commrat_generate_schema: TARGET '${CGS_TARGET}' is not a known CMake target")
    endif()

    if(NOT IS_ABSOLUTE "${CGS_APP_HEADER}")
        message(FATAL_ERROR
            "commrat_generate_schema: APP_HEADER must be an absolute path (got: ${CGS_APP_HEADER})")
    endif()

    # ---- Locate the driver source --------------------------------------------
    # Search order:
    #   1. COMMRAT_SCHEMA_GEN_DRIVER override (advanced users / CI)
    #   2. Source tree relative to this module file (dev workflow)
    #   3. Installed path under share/commrat/schema_gen/ (installed package)
    if(DEFINED COMMRAT_SCHEMA_GEN_DRIVER)
        set(_driver_src "${COMMRAT_SCHEMA_GEN_DRIVER}")
    elseif(EXISTS "${_COMMRAT_SCHEMA_GEN_MODULE_DIR}/../tools/commrat-inspect/schema_gen_driver.cpp")
        get_filename_component(_driver_src
            "${_COMMRAT_SCHEMA_GEN_MODULE_DIR}/../tools/commrat-inspect/schema_gen_driver.cpp"
            ABSOLUTE)
    else()
        find_file(_driver_src schema_gen_driver.cpp
            PATHS "${CMAKE_INSTALL_PREFIX}/share/commrat/schema_gen"
            NO_DEFAULT_PATH)
        if(NOT _driver_src)
            message(FATAL_ERROR
                "commrat_generate_schema: cannot locate schema_gen_driver.cpp.\n"
                "Set COMMRAT_SCHEMA_GEN_DRIVER to its absolute path or install CommRaT.")
        endif()
    endif()

    # ---- Build the schema-gen executable ------------------------------------
    set(_gen_target "${CGS_TARGET}_schema_gen")

    add_executable(${_gen_target} "${_driver_src}")

    # Link CommRaT (header-only) + inherit include dirs from the user's target
    # so APP_HEADER and its transitive includes are resolvable.
    target_link_libraries(${_gen_target} PRIVATE commrat)
    target_include_directories(${_gen_target}
        PRIVATE
            $<TARGET_PROPERTY:${CGS_TARGET},INCLUDE_DIRECTORIES>
            $<TARGET_PROPERTY:${CGS_TARGET},INTERFACE_INCLUDE_DIRECTORIES>
    )

    # ---- Compile definitions ------------------------------------------------
    # Wrap header path in escaped quotes so it becomes a string literal usable
    # as an #include argument inside the driver (e.g. #include COMMRAT_APP_HEADER).
    target_compile_definitions(${_gen_target}
        PRIVATE
            "COMMRAT_APP_HEADER=\"${CGS_APP_HEADER}\""
            "COMMRAT_APP_TYPE=${CGS_APP_TYPE}"
            "COMMRAT_OUTPUT_FILE=\"${CGS_OUTPUT}\""
    )

    # ---- Post-build: run the driver to emit the JSON ------------------------
    add_custom_command(
        TARGET ${CGS_TARGET} POST_BUILD
        COMMAND ${_gen_target}
        BYPRODUCTS "${CGS_OUTPUT}"
        COMMENT "Generating CommRaT schema: ${CGS_OUTPUT}"
        VERBATIM
    )

    # Ensure the driver is built before it is run.
    add_dependencies(${CGS_TARGET} ${_gen_target})

    message(STATUS
        "commrat_generate_schema: ${CGS_TARGET} -> ${CGS_OUTPUT} (type: ${CGS_APP_TYPE})")
endfunction()
