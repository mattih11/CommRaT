# cmake/isar-sdk-toolchain.cmake
#
# CMake toolchain for building CommRaT against the RaTOS ISAR SDK.
#
# The RaTOS SDK is a Debian Trixie amd64 sysroot containing libevl, RACK,
# SeRTial, and all CommRaT dependencies.  The SDK ships its own gcc-14 with
# a sysroot-wrapper (gcc-sysroot-wrapper.sh) that auto-injects
# --sysroot=<sdk> into every compiler invocation.
#
# Usage (typically invoked via evl-dev.sh, not directly):
#   scripts/evl-dev.sh --cross          # auto-downloads + relocates SDK
#   scripts/evl-dev.sh --cross --test   # compile + test in QEMU
#
# If invoking cmake directly, export EVL_SDK_DIR first:
#   export EVL_SDK_DIR=.evl-cache/sdk && cmake --preset evl-cross
#
# SDK extraction + relocation (done automatically by scripts/evl-dev.sh --cross):
#   mkdir -p .evl-cache/sdk
#   tar -xJf ratos-dev-sdk-container-amd64.xz -C .evl-cache/sdk --strip-components=1
#   sed -i 's|^GCC_SYSROOT=.*|GCC_SYSROOT=".evl-cache/sdk"|' .evl-cache/sdk/usr/bin/gcc-sysroot-wrapper.sh

if(DEFINED ENV{EVL_SDK_DIR})
    # Resolve the path and (re)populate the cache entry.
    if(IS_ABSOLUTE "$ENV{EVL_SDK_DIR}")
        set(_EVL_SDK "$ENV{EVL_SDK_DIR}" CACHE PATH "RaTOS ISAR SDK root" FORCE)
    else()
        # Resolve relative paths (e.g. EVL_SDK_DIR=.evl-cache/sdk) against the
        # source tree.  Relative paths in the CMake cache break TryCompile.
        set(_EVL_SDK "${CMAKE_SOURCE_DIR}/$ENV{EVL_SDK_DIR}" CACHE PATH "RaTOS ISAR SDK root" FORCE)
    endif()
elseif(NOT DEFINED CACHE{_EVL_SDK})
    message(FATAL_ERROR
        "EVL_SDK_DIR environment variable is not set.\n"
        "Run 'scripts/evl-dev.sh --cross' to auto-download the SDK to .evl-cache/sdk,\n"
        "or export EVL_SDK_DIR=/path/to/sdk before invoking cmake directly.")
endif()

# Use the SDK's own cross-compiler.  After relocation, the gcc-sysroot-wrapper.sh
# script auto-injects --sysroot=<sdk> so that all system headers and libraries
# are resolved from the SDK sysroot, not the host.
set(CMAKE_C_COMPILER   "${_EVL_SDK}/usr/bin/x86_64-linux-gnu-gcc" CACHE FILEPATH "C compiler"   FORCE)
set(CMAKE_CXX_COMPILER "${_EVL_SDK}/usr/bin/x86_64-linux-gnu-g++" CACHE FILEPATH "C++ compiler" FORCE)

# The SDK's gcc wrapper (gcc-sysroot-wrapper.sh) already injects
# --sysroot=<sdk> into every compiler invocation after relocation.
# Do NOT set CMAKE_SYSROOT here: cmake probes the compiler with --sysroot,
# discovers <sdk>/usr/include as an implicit include directory, and injects
# -isystem <sdk>/usr/include into compile commands.  That extra -isystem
# disrupts the C++ stdlib #include_next chain.
#
# Instead, set CMAKE_FIND_ROOT_PATH so cmake's find_package / find_library /
# find_path search the SDK sysroot.  The compiler handles the actual sysroot.
set(CMAKE_FIND_ROOT_PATH "${_EVL_SDK}")

# Add the SDK usr/ prefix to cmake's find_package search paths so that
# RACKConfig.cmake, SeRTialConfig.cmake, reflectcpp-config.cmake, etc. are
# found in the SDK before any host copies.
list(PREPEND CMAKE_PREFIX_PATH "${_EVL_SDK}/usr")

# Find headers/libraries inside the sysroot; also allow cmake to search the
# host for programs (e.g., cmake itself, pkg-config).
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
