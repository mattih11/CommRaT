cmake_minimum_required(VERSION 3.16)
project(CommRaT VERSION 1.0.0 LANGUAGES CXX)

# Modern C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use GNUInstallDirs for standard installation directories
include(GNUInstallDirs)

# Find SeRTial package (installed system-wide)
find_package(SeRTial REQUIRED)

# Real-time compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")

# Library target
add_library(commrat STATIC
    src/tims_wrapper.cpp
)

target_include_directories(commrat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Include TIMS and RACK headers
target_include_directories(commrat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tims
    /usr/local/include/rack
)

# Link against librack from /usr/local/lib
link_directories(/usr/local/lib)

target_link_libraries(commrat PUBLIC
    SeRTial::sertial
    rack
    pthread
    rt
)

# Example applications

# Examples demonstrating current API (3-mailbox architecture)
add_executable(example_continuous_input examples/continuous_input_example.cpp)
target_link_libraries(example_continuous_input PRIVATE commrat)
target_include_directories(example_continuous_input PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

add_executable(example_clean_interface examples/clean_interface_example.cpp)
target_link_libraries(example_clean_interface PRIVATE commrat)
target_include_directories(example_clean_interface PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

add_executable(example_commands examples/command_example.cpp)
target_link_libraries(example_commands PRIVATE commrat)
target_include_directories(example_commands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

add_executable(example_loop_mode examples/loop_mode_example.cpp)
target_link_libraries(example_loop_mode PRIVATE commrat)
target_include_directories(example_loop_mode PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

add_executable(example_multi_output_runtime examples/multi_output_runtime.cpp)
target_link_libraries(example_multi_output_runtime PRIVATE commrat)
target_include_directories(example_multi_output_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

# Test applications
add_executable(test_io_spec test/test_io_spec.cpp)
target_link_libraries(test_io_spec PRIVATE commrat)
target_include_directories(test_io_spec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

add_executable(test_process_signature test/test_process_signature.cpp)
target_link_libraries(test_process_signature PRIVATE commrat)
target_include_directories(test_process_signature PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

add_executable(test_multi_output test/test_multi_output.cpp)
target_link_libraries(test_multi_output PRIVATE commrat)
target_include_directories(test_multi_output PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)

# Install targets
install(TARGETS commrat
    EXPORT CommRaTTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install examples (optional - typically not installed with library)
install(TARGETS example_continuous_input example_clean_interface example_commands example_loop_mode
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
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
