cmake_minimum_required(VERSION 3.16)
project(CommRaT VERSION 1.0.0 LANGUAGES CXX)

# Modern C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Real-time compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/SeRTial/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tims
    /usr/local/include/rack
)

# Library target
add_library(commrat STATIC
    src/tims_wrapper.cpp
    src/message_service.cpp
)

target_include_directories(commrat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link against librack from /usr/local/lib
link_directories(/usr/local/lib)

target_link_libraries(commrat PUBLIC
    rack
    pthread
    rt
)

# Example applications
add_executable(example_sender examples/sender.cpp)
target_link_libraries(example_sender PRIVATE commrat)

add_executable(example_receiver examples/receiver.cpp)
target_link_libraries(example_receiver PRIVATE commrat)

add_executable(example_simple_usage examples/simple_usage.cpp)
target_link_libraries(example_simple_usage PRIVATE commrat)

add_executable(example_registry_demo examples/registry_demo.cpp)
target_link_libraries(example_registry_demo PRIVATE commrat)

add_executable(example_custom_messages examples/custom_messages.cpp)
target_link_libraries(example_custom_messages PRIVATE commrat)

# Test executables
add_executable(test_sender test/test_sender.cpp)
target_link_libraries(test_sender PRIVATE commrat)

add_executable(test_receiver test/test_receiver.cpp)
target_link_libraries(test_receiver PRIVATE commrat)

# Install targets
install(TARGETS commrat example_sender example_receiver example_simple_usage example_registry_demo
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
