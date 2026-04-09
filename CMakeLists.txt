# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.30...4.3)

project(
    beman.std-big-int # CMake Project Name, which is also the name of the top-level
    # targets (e.g., library, executable, etc.).
    DESCRIPTION "Reference implementation of std::big_int"
    LANGUAGES CXX
    VERSION 0.1.0
)

# [CMAKE.SKIP_TESTS]
option(
    BEMAN_STD-BIG-INT_BUILD_TESTS
    "Enable building tests and test infrastructure. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# [CMAKE.SKIP_EXAMPLES]
option(
    BEMAN_STD-BIG-INT_BUILD_EXAMPLES
    "Enable building examples. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# for find of beman-install-library
include(infra/cmake/beman-install-library.cmake)

add_library(beman.std-big-int INTERFACE)
add_library(beman::std-big-int ALIAS beman.std-big-int)

target_sources(
    beman.std-big-int
    PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

set_target_properties(
    beman.std-big-int
    PROPERTIES VERIFY_INTERFACE_HEADER_SETS ${PROJECT_IS_TOP_LEVEL}
)

add_subdirectory(include/beman/std-big-int)

beman_install_library(beman.std-big-int TARGETS beman.std-big-int)

if(BEMAN_STD-BIG-INT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/beman/std-big-int)
endif()

if(BEMAN_STD-BIG-INT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
