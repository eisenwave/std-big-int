# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.30...4.3)

project(
    beman.big_int
    DESCRIPTION "Reference implementation of std::big_int"
    LANGUAGES CXX
    VERSION 0.1.0
)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL
        https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# [CMAKE.SKIP_TESTS]
option(
    BEMAN_BIG_INT_BUILD_TESTS
    "Enable building tests and test infrastructure. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# [CMAKE.SKIP_EXAMPLES]
option(
    BEMAN_BIG_INT_BUILD_EXAMPLES
    "Enable building examples. Default: ${PROJECT_IS_TOP_LEVEL}. Values: { ON, OFF }."
    ${PROJECT_IS_TOP_LEVEL}
)

# for find of beman-install-library
include(infra/cmake/beman-install-library.cmake)

add_library(beman.big_int INTERFACE)
add_library(beman::big_int ALIAS beman.big_int)
target_compile_features(beman.big_int INTERFACE cxx_std_23)

target_sources(
    beman.big_int
    PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

set_target_properties(
    beman.big_int
    PROPERTIES VERIFY_INTERFACE_HEADER_SETS ${PROJECT_IS_TOP_LEVEL}
)

add_subdirectory(include/beman/big_int)

beman_install_library(beman.big_int TARGETS beman.big_int)

if(BEMAN_BIG_INT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/beman/big_int)
endif()

if(BEMAN_BIG_INT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
