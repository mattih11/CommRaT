cmake_minimum_required(VERSION 3.20)
project(HelloCommRaT CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find CommRaT library
# If CommRaT is installed system-wide, this should work automatically
# Otherwise, adjust CMAKE_PREFIX_PATH or provide hints
find_package(CommRaT REQUIRED)

# Find TiMS library (required dependency)
find_library(TIMS_LIBRARY NAMES tims REQUIRED)

# Create executable
add_executable(hello_commrat
    hello_commrat.cpp
)

# Link libraries
target_link_libraries(hello_commrat
    PRIVATE
        CommRaT::commrat    # CommRaT framework
        ${TIMS_LIBRARY}     # TiMS messaging layer
        pthread             # POSIX threads
)

# Enable warnings
target_compile_options(hello_commrat
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3>
)

# Print build information
message(STATUS "Building Hello CommRaT example")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  CommRaT: ${CommRaT_DIR}")
