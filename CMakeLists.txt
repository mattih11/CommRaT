cmake_minimum_required(VERSION 3.21)
project(CommRaT VERSION 1.0.0 LANGUAGES CXX)

# Modern C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use GNUInstallDirs for standard installation directories
include(GNUInstallDirs)
include(cmake/CommRaTMacros.cmake)
include(cmake/CommRaTSchemaGen.cmake)

find_package(CoreRaT REQUIRED)

option(COMMRAT_BUILD_EXAMPLES "Build CommRaT examples" ON)
option(COMMRAT_BUILD_TESTS    "Build CommRaT tests"    ON)

add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options($<$<CONFIG:Release>:-O3> $<$<CONFIG:Release>:-march=native>)

# Library target — header-only; CoreRaT provides the IPC backend
add_library(commrat INTERFACE)

target_include_directories(commrat INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Link the appropriate CoreRaT IPC backend
if(TARGET CoreRaT::corerat_tims)
    # STD platform: CoreRaT was built with the self-contained TCP/TiMS backend
    target_link_libraries(commrat INTERFACE CoreRaT::corerat_tims)
    message(STATUS "CommRaT: using CoreRaT TiMS backend (STD platform)")
else()
    # EVL platform: CoreRaT was built with the EVL-native IPC backend (header-only)
    target_link_libraries(commrat INTERFACE CoreRaT::corerat)
    message(STATUS "CommRaT: using CoreRaT EVL backend")
endif()

if(COMMRAT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

enable_testing()

if(COMMRAT_BUILD_TESTS)
    add_subdirectory(test)
endif()

#-----------------------------------------------------------------------------
# commrat CLI binary
#-----------------------------------------------------------------------------
add_executable(commrat-cli src/commrat_main.cpp)
set_target_properties(commrat-cli PROPERTIES OUTPUT_NAME commrat)
target_link_libraries(commrat-cli PRIVATE commrat)

#-----------------------------------------------------------------------------
# Install
#-----------------------------------------------------------------------------
install(TARGETS commrat commrat-cli
    EXPORT CommRaTTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers
install(DIRECTORY include/commrat
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Export targets for downstream projects
install(EXPORT CommRaTTargets
    FILE CommRaTTargets.cmake
    NAMESPACE CommRaT::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommRaT
)

# Generate and install package config file
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CommRaTConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CommRaTConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommRaT
)

# Generate version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CommRaTConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CommRaTConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CommRaTConfigVersion.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CommRaTMacros.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CommRaTSchemaGen.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommRaT
)

# Install commrat-inspect tools (web viewer + schema-gen driver source)
install(FILES
    tools/commrat-inspect/viewer.html
    tools/commrat-inspect/schema_gen_driver.cpp
    DESTINATION ${CMAKE_INSTALL_DATADIR}/commrat/commrat-inspect
)

#-----------------------------------------------------------------------------
# Container-based development targets
#
# These targets let you build and test inside the RaTOS dev container without
# opening a Dev Container session. They are skipped automatically when already
# running inside a container (GitHub Actions and Dev Containers both set the
# 'container' environment variable).
#
# Usage (from a cmake --preset default build dir):
#   cmake --build --preset default --target commrat-shell
#   cmake --build --preset default --target commrat-evl-dev
#   cmake --build --preset default --target commrat-test-container
#   cmake --build --preset default --target commrat-evl-compile
#
# Alternatively, use scripts/evl-dev.sh directly (more flexible).
#-----------------------------------------------------------------------------
if(NOT DEFINED ENV{container})
    find_program(COMMRAT_CONTAINER_RUNTIME NAMES docker podman)
    if(COMMRAT_CONTAINER_RUNTIME)
        set(RATOS_IMAGE_REF "ghcr.io/mattih11/ratos-dev-image:latest"
            CACHE STRING "RaTOS dev container image used by commrat-* targets")

        add_custom_target(commrat-shell
            COMMAND ${COMMRAT_CONTAINER_RUNTIME} run --rm -it
                --network host
                --volume "${CMAKE_SOURCE_DIR}:/workspace"
                --workdir /workspace
                ${RATOS_IMAGE_REF}
                bash
            USES_TERMINAL
            COMMENT "Opening interactive shell in RaTOS dev container (${RATOS_IMAGE_REF})"
            VERBATIM
        )

        add_custom_target(commrat-evl-dev
            COMMAND ${COMMRAT_CONTAINER_RUNTIME} run --rm -it
                --network host
                --volume "${CMAKE_SOURCE_DIR}:/workspace"
                --workdir /workspace
                ${RATOS_IMAGE_REF}
                bash -c
                    "set -euo pipefail && \
                     echo '--- Building EVL preset ---' && \
                     cmake --preset evl && \
                     cmake --build --preset evl --parallel $(nproc) && \
                     echo '--- Build complete; starting shell (build output in build/evl/) ---' && \
                     exec bash"
            USES_TERMINAL
            COMMENT "Build EVL platform inside RaTOS dev container, then open shell"
            VERBATIM
        )

        add_custom_target(commrat-test-container
            COMMAND ${COMMRAT_CONTAINER_RUNTIME} run --rm
                --network host
                --volume "${CMAKE_SOURCE_DIR}:/workspace"
                --workdir /workspace
                ${RATOS_IMAGE_REF}
                bash -c "cmake --preset default && cmake --build --preset default && ctest --preset default"
            USES_TERMINAL
            COMMENT "Building and testing CommRaT inside RaTOS dev container"
            VERBATIM
        )

        add_custom_target(commrat-evl-compile
            COMMAND ${COMMRAT_CONTAINER_RUNTIME} run --rm
                --network host
                --volume "${CMAKE_SOURCE_DIR}:/workspace"
                --workdir /workspace
                ${RATOS_IMAGE_REF}
                bash -c "cmake --preset evl && cmake --build --preset evl"
            USES_TERMINAL
            COMMENT "EVL platform compile-check inside RaTOS dev container"
            VERBATIM
        )
    endif()
endif()

#-----------------------------------------------------------------------------
# Documentation with Doxygen
#-----------------------------------------------------------------------------
find_package(Doxygen QUIET COMPONENTS dot)

if(DOXYGEN_FOUND)
    message(STATUS "Doxygen found: ${DOXYGEN_VERSION}")
    
    # Doxygen target
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    
    message(STATUS "Documentation target 'docs' available - run 'make docs' to generate")
else()
    message(STATUS "Doxygen not found - documentation target not available")
    message(STATUS "Install doxygen and graphviz: sudo apt-get install doxygen graphviz")
endif()
