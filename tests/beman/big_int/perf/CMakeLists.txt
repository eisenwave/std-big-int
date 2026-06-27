# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

include(FetchContent)

# Pull in Google Benchmark for the big_int-vs-builtin micro-benchmarks, mirroring
# how the top-level build fetches GoogleTest. Only fetched when
# BEMAN_BIG_INT_BUILD_BENCHMARKS is enabled (this directory is added from the
# top-level CMakeLists under that option).
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
# Google Benchmark builds itself with -Werror by default, which can break on
# newer compilers than its release was tested against; turn it off.
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
    URL_HASH
        SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
)
FetchContent_MakeAvailable(googlebenchmark)

# Benchmark sources use the `.gbench.cpp` suffix so they are never confused with
# the standalone GMP/boost comparison programs (`*.perf.cpp`, `*.limbs.cpp`) that
# also live in this directory and are compiled by hand.
file(GLOB BENCHMARK_SOURCES "*.gbench.cpp")

foreach(benchmark_source ${BENCHMARK_SOURCES})
    get_filename_component(benchmark_name ${benchmark_source} NAME_WE)
    set(benchmark_target beman.big_int.benchmarks.${benchmark_name})

    add_executable(${benchmark_target})
    target_sources(${benchmark_target} PRIVATE ${benchmark_source})
    target_link_libraries(
        ${benchmark_target}
        PRIVATE beman::big_int benchmark::benchmark
    )
    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"
    )
        target_compile_options(${benchmark_target} PRIVATE -Wall -Wextra)
    elseif(MSVC)
        target_compile_options(${benchmark_target} PRIVATE /W4)
    endif()
endforeach()
