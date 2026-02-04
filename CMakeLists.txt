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
add_executable(example_simple_usage examples/simple_usage.cpp)
target_link_libraries(example_simple_usage PRIVATE commrat)

add_executable(example_registry_demo examples/registry_demo.cpp)
target_link_libraries(example_registry_demo PRIVATE commrat)

# Mailbox example
add_executable(example_mailbox examples/mailbox_example.cpp)
target_link_libraries(example_mailbox PRIVATE commrat)

# Nonblocking test
add_executable(test_nonblocking examples/test_nonblocking.cpp)
target_link_libraries(test_nonblocking PRIVATE commrat)

# Install targets
install(TARGETS commrat example_simple_usage example_registry_demo example_mailbox test_nonblocking
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
