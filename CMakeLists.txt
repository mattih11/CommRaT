cmake_minimum_required(VERSION 3.16)
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
