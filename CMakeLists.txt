cmake_minimum_required(VERSION 3.21)
project(CommRaT VERSION 1.0.0 LANGUAGES CXX)

# Modern C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use GNUInstallDirs for standard installation directories
include(GNUInstallDirs)

find_package(SeRTial REQUIRED)
find_package(RACK REQUIRED)
find_package(Threads REQUIRED)

option(COMMRAT_BUILD_EXAMPLES "Build CommRaT examples" ON)
option(COMMRAT_BUILD_TESTS    "Build CommRaT tests"    ON)

add_compile_options(-Wall -Wextra -Wpedantic)
add_compile_options($<$<CONFIG:Release>:-O3> $<$<CONFIG:Release>:-march=native>)

# Library target
add_library(commrat STATIC
    src/tims_wrapper.cpp
)

target_include_directories(commrat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(commrat PUBLIC
    SeRTial::sertial
    RACK::rack
    Threads::Threads
    rt
)

# Platform backend selection
set(COMMRAT_PLATFORM "STD" CACHE STRING "Platform backend (STD or EVL)")
set_property(CACHE COMMRAT_PLATFORM PROPERTY STRINGS STD EVL)

if(NOT COMMRAT_PLATFORM MATCHES "^(STD|EVL)$")
    message(FATAL_ERROR "COMMRAT_PLATFORM must be STD or EVL, got: ${COMMRAT_PLATFORM}")
endif()

target_compile_definitions(commrat PUBLIC COMMRAT_PLATFORM_${COMMRAT_PLATFORM})

if(COMMRAT_PLATFORM STREQUAL "EVL")
    find_library(EVL_LIBRARY NAMES evl REQUIRED
        DOC "libevl runtime library (Xenomai 4 / libevl)")
    find_path(EVL_INCLUDE_DIR NAMES evl/thread.h REQUIRED
        DOC "libevl include directory")
    add_library(EVL::evl UNKNOWN IMPORTED)
    set_target_properties(EVL::evl PROPERTIES
        IMPORTED_LOCATION "${EVL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${EVL_INCLUDE_DIR}")
    target_link_libraries(commrat PUBLIC EVL::evl)
    message(STATUS "CommRaT platform: EVL (Xenomai 4 / libevl)")
else()
    message(STATUS "CommRaT platform: STD (standard Linux)")
endif()

if(COMMRAT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

enable_testing()

if(COMMRAT_BUILD_TESTS)
    add_subdirectory(test)
endif()

#-----------------------------------------------------------------------------
# Install
#-----------------------------------------------------------------------------
install(TARGETS commrat
    EXPORT CommRaTTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
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
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CommRaT
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
#   cmake --build --preset default --target commrat-test-container
#   cmake --build --preset default --target commrat-evl-compile
#-----------------------------------------------------------------------------
if(NOT DEFINED ENV{container})
    find_program(COMMRAT_CONTAINER_RUNTIME docker podman)
    if(COMMRAT_CONTAINER_RUNTIME)
        set(RATOS_IMAGE_REF "ghcr.io/mattih11/ratos-dev-image:latest"
            CACHE STRING "RaTOS dev container image used by commrat-* targets")

        add_custom_target(commrat-shell
            COMMAND ${COMMRAT_CONTAINER_RUNTIME} run --rm -it
                --volume "${CMAKE_SOURCE_DIR}:/workspace"
                --workdir /workspace
                ${RATOS_IMAGE_REF}
                bash
            USES_TERMINAL
            COMMENT "Opening interactive shell in RaTOS dev container (${RATOS_IMAGE_REF})"
            VERBATIM
        )

        add_custom_target(commrat-test-container
            COMMAND ${COMMRAT_CONTAINER_RUNTIME} run --rm
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
