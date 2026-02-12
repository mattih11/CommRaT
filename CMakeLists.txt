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

# Include RACK headers (includes TIMS)
target_include_directories(commrat PRIVATE
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
target_include_directories(example_continuous_input PRIVATE /usr/local/include/rack)

add_executable(example_clean_interface examples/clean_interface_example.cpp)
target_link_libraries(example_clean_interface PRIVATE commrat)
target_include_directories(example_clean_interface PRIVATE /usr/local/include/rack)

add_executable(example_commands examples/command_example.cpp)
target_link_libraries(example_commands PRIVATE commrat)
target_include_directories(example_commands PRIVATE /usr/local/include/rack)

add_executable(example_loop_mode examples/loop_mode_example.cpp)
target_link_libraries(example_loop_mode PRIVATE commrat)
target_include_directories(example_loop_mode PRIVATE /usr/local/include/rack)

add_executable(example_multi_output_runtime examples/multi_output_runtime.cpp)
target_link_libraries(example_multi_output_runtime PRIVATE commrat)
target_include_directories(example_multi_output_runtime PRIVATE /usr/local/include/rack)

add_executable(example_sensor_fusion examples/multi_output_sensor_fusion.cpp)
target_link_libraries(example_sensor_fusion PRIVATE commrat)
target_include_directories(example_sensor_fusion PRIVATE /usr/local/include/rack)

# Documentation examples (ensure they always compile and work)
add_executable(doc_example_hello_commrat docs/examples/01_hello_commrat/hello_commrat.cpp)
target_link_libraries(doc_example_hello_commrat PRIVATE commrat)
target_include_directories(doc_example_hello_commrat PRIVATE /usr/local/include/rack)

add_executable(doc_example_multi_output docs/examples/02_multi_output/multi_output_example.cpp)
target_link_libraries(doc_example_multi_output PRIVATE commrat)
target_include_directories(doc_example_multi_output PRIVATE /usr/local/include/rack)

add_executable(doc_example_multi_input_fusion docs/examples/03_multi_input_fusion/multi_input_fusion.cpp)
target_link_libraries(doc_example_multi_input_fusion PRIVATE commrat)
target_include_directories(doc_example_multi_input_fusion PRIVATE /usr/local/include/rack)

# Enable testing
enable_testing()

# Test applications
add_executable(test_io_spec test/test_io_spec.cpp)
target_link_libraries(test_io_spec PRIVATE commrat)
target_include_directories(test_io_spec PRIVATE /usr/local/include/rack)
add_test(NAME test_io_spec COMMAND test_io_spec)

add_executable(test_process_signature test/test_process_signature.cpp)
target_link_libraries(test_process_signature PRIVATE commrat)
target_include_directories(test_process_signature PRIVATE /usr/local/include/rack)
add_test(NAME test_process_signature COMMAND test_process_signature)

add_executable(test_multi_output test/test_multi_output.cpp)
target_link_libraries(test_multi_output PRIVATE commrat)
target_include_directories(test_multi_output PRIVATE /usr/local/include/rack)
add_test(NAME test_multi_output COMMAND test_multi_output)

add_executable(test_primary_input test/test_primary_input.cpp)
target_link_libraries(test_primary_input PRIVATE commrat)
target_include_directories(test_primary_input PRIVATE /usr/local/include/rack)
add_test(NAME test_primary_input COMMAND test_primary_input)

add_executable(test_timestamped_ring_buffer test/test_timestamped_ring_buffer.cpp)
target_link_libraries(test_timestamped_ring_buffer PRIVATE commrat)
target_include_directories(test_timestamped_ring_buffer PRIVATE /usr/local/include/rack)
add_test(NAME test_timestamped_ring_buffer COMMAND test_timestamped_ring_buffer)

add_executable(test_historical_mailbox test/test_historical_mailbox.cpp)
target_link_libraries(test_historical_mailbox PRIVATE commrat)
target_include_directories(test_historical_mailbox PRIVATE /usr/local/include/rack)
add_test(NAME test_historical_mailbox COMMAND test_historical_mailbox)

# Phase 6.4: Multi-input synchronization test
add_executable(test_multi_input test/test_multi_input.cpp)
target_link_libraries(test_multi_input PRIVATE commrat)
target_include_directories(test_multi_input PRIVATE /usr/local/include/rack)
add_test(NAME test_multi_input COMMAND test_multi_input)

# Phase 6.6: Multi-input Module integration test
add_executable(test_multi_input_module test/test_multi_input_module.cpp)
target_link_libraries(test_multi_input_module PRIVATE commrat)
target_include_directories(test_multi_input_module PRIVATE /usr/local/include/rack)
add_test(NAME test_multi_input_module COMMAND test_multi_input_module)

# Phase 6.8: Multi-input compilation tests (type safety validation)
add_executable(test_multi_input_compilation test/test_multi_input_compilation.cpp)
target_link_libraries(test_multi_input_compilation PRIVATE commrat)
target_include_directories(test_multi_input_compilation PRIVATE /usr/local/include/rack)
add_test(NAME test_multi_input_compilation COMMAND test_multi_input_compilation)

# Phase 6.9: End-to-end 3-input sensor fusion test
add_executable(test_3input_fusion test/test_3input_fusion.cpp)
target_link_libraries(test_3input_fusion PRIVATE commrat)
target_include_directories(test_3input_fusion PRIVATE /usr/local/include/rack)
add_test(NAME test_3input_fusion COMMAND test_3input_fusion)

# Phase 6.10: Automatic timestamp management verification
add_executable(test_timestamp_logic test/test_timestamp_logic.cpp)
target_link_libraries(test_timestamp_logic PRIVATE commrat)
target_include_directories(test_timestamp_logic PRIVATE /usr/local/include/rack)
add_test(NAME test_timestamp_logic COMMAND test_timestamp_logic)

# Phase 7: Mailbox buffer sizing optimization test
add_executable(test_mailbox_sizing test/test_mailbox_sizing.cpp)
target_link_libraries(test_mailbox_sizing PRIVATE commrat)
target_include_directories(test_mailbox_sizing PRIVATE /usr/local/include/rack)
add_test(NAME test_mailbox_sizing COMMAND test_mailbox_sizing)

# Phase 7: TypedMailbox compile-time type validation test
add_executable(test_typed_mailbox test/test_typed_mailbox.cpp)
target_link_libraries(test_typed_mailbox PRIVATE commrat)
target_include_directories(test_typed_mailbox PRIVATE /usr/local/include/rack)
add_test(NAME test_typed_mailbox COMMAND test_typed_mailbox)

# Add examples as tests (use wrapper for continuous examples)
add_test(NAME example_continuous_input COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_continuous_example.sh $<TARGET_FILE:example_continuous_input>)
add_test(NAME example_clean_interface COMMAND example_clean_interface)
add_test(NAME example_commands COMMAND example_commands)
add_test(NAME example_loop_mode COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_continuous_example.sh $<TARGET_FILE:example_loop_mode>)
add_test(NAME example_multi_output_runtime COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_continuous_example.sh $<TARGET_FILE:example_multi_output_runtime>)
add_test(NAME example_sensor_fusion COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_continuous_example.sh $<TARGET_FILE:example_sensor_fusion>)

# Add documentation examples as tests (ensure docs stay current)
add_test(NAME doc_example_hello_commrat COMMAND doc_example_hello_commrat)
add_test(NAME doc_example_multi_output COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_continuous_example.sh $<TARGET_FILE:doc_example_multi_output>)
add_test(NAME doc_example_multi_input_fusion COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_continuous_example.sh $<TARGET_FILE:doc_example_multi_input_fusion>)


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

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_COMMAND} -E echo "=== Running CommRaT Test Suite ==="
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_timestamp_logic ---"
    COMMAND $<TARGET_FILE:test_timestamp_logic> 2>&1 | grep -E "(===|PASS|FAIL)" || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_3input_fusion ---"
    COMMAND $<TARGET_FILE:test_3input_fusion> 2>&1 | tail -5 || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_historical_mailbox ---"
    COMMAND $<TARGET_FILE:test_historical_mailbox> 2>&1 | grep -E "(===|PASS|FAIL)" || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_process_signature ---"
    COMMAND $<TARGET_FILE:test_process_signature> 2>&1 | tail -3 || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_timestamped_ring_buffer ---"
    COMMAND $<TARGET_FILE:test_timestamped_ring_buffer> 2>&1 | grep -E "(===|PASS|Test)" || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_multi_output ---"
    COMMAND $<TARGET_FILE:test_multi_output> 2>&1 | tail -3 || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_multi_input ---"
    COMMAND $<TARGET_FILE:test_multi_input> 2>&1 | tail -3 || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_multi_input_module ---"
    COMMAND $<TARGET_FILE:test_multi_input_module> 2>&1 | tail -3 || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "--- test_multi_input_compilation ---"
    COMMAND $<TARGET_FILE:test_multi_input_compilation> 2>&1 | tail -3 || true
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "=== Test Suite Complete ==="
    DEPENDS test_timestamp_logic test_3input_fusion test_historical_mailbox test_process_signature test_timestamped_ring_buffer test_multi_output test_multi_input test_multi_input_module test_multi_input_compilation
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running all CommRaT tests"
    VERBATIM
)

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
