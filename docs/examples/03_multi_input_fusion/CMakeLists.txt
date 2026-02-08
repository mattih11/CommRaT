cmake_minimum_required(VERSION 3.16)
project(MultiInputFusionExample CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find SeRTial package
find_package(SeRTial REQUIRED)

# Set path to parent CommRaT project
set(COMMRAT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# Include TIMS and RACK headers
include_directories(
    ${COMMRAT_ROOT}/include
    ${COMMRAT_ROOT}/tims
    /usr/local/include/rack
)

# Link against librack from /usr/local/lib
link_directories(/usr/local/lib)

# Create executable
add_executable(multi_input_fusion
    multi_input_fusion.cpp
    ${COMMRAT_ROOT}/src/tims_wrapper.cpp
)

# Link libraries
target_link_libraries(multi_input_fusion
    PRIVATE
        SeRTial::sertial    # Serialization library
        rack                # RACK/TiMS library
        pthread             # POSIX threads
        rt                  # Real-time extensions
)

# Enable warnings
target_compile_options(multi_input_fusion
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O3>
)
