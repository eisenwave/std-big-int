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

add_library(
    beman.big_int
    src/divide.cpp
    src/karatsuba.cpp
    src/mul_dispatch.cpp
    src/mulmod_bnm1.cpp
    src/toom_cook_3.cpp
    src/toom_cook_4.cpp
    src/toom_cook_6_5.cpp
    src/toom_cook_8_5.cpp
)
add_library(beman::big_int ALIAS beman.big_int)
target_compile_features(beman.big_int PUBLIC cxx_std_23)

# Multiplication's FFT tier. By default it is an integer Montgomery NTT
# (src/ntt.cpp, src/fft_mul.cpp): pure integer arithmetic, exact on every
# conforming compiler with no floating-point-environment assumptions.
#
# BEMAN_BIG_INT_SIMD_MUL switches it to the faster double-precision FP NTT with
# hand-written NEON/AVX2 SIMD kernels (src/ntt_fp_*.cpp, src/fft_mul_fp.cpp). That
# path is exact ONLY under round-to-nearest with no fast-math / FMA-contraction;
# this build forbids those per TU below, but if you build with another system you
# must guarantee that floating-point environment yourself. Opt in only then.
option(
    BEMAN_BIG_INT_SIMD_MUL
    "Use the double-precision FP NTT with SIMD (NEON/AVX2) for FFT multiplication. Requires round-to-nearest and no fast-math / FMA contraction. Default: OFF (the integer NTT)."
    OFF
)

if(BEMAN_BIG_INT_SIMD_MUL)
    # The dispatch lives in a header (mul_impl.hpp), so the library and every
    # consumer must agree on the path: propagate the macro PUBLICly.
    target_compile_definitions(beman.big_int PUBLIC BEMAN_BIG_INT_SIMD_MUL)

    target_sources(
        beman.big_int
        PRIVATE src/ntt_fp_scalar.cpp src/ntt_fp_dispatch.cpp src/fft_mul_fp.cpp
    )

    # The FP NTT relies on round-to-nearest and on its explicit FMAs being the ONLY
    # fused operations (the exactness proof needs h = round(a*b) and l = fma(a,b,-h)
    # as distinct roundings). Forbid fast-math / FP contraction on every FP TU.
    set(_bigint_fp_strict
        "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off;-fno-fast-math>$<$<CXX_COMPILER_ID:MSVC>:/fp:strict>"
    )
    set_source_files_properties(
        src/ntt_fp_scalar.cpp
        src/ntt_fp_dispatch.cpp
        src/fft_mul_fp.cpp
        PROPERTIES COMPILE_OPTIONS "${_bigint_fp_strict}"
    )

    # Hand-written SIMD kernels, one ISA per TU, gated by target architecture.
    # AArch64: NEON is mandatory baseline (no runtime dispatch, no extra ISA flags).
    # x86-64: the AVX2+FMA kernel TU gets -mavx2 -mfma (or /arch:AVX2); the dispatcher
    # (baseline ISA) selects it only after a runtime AVX2 check, so AVX instructions
    # stay confined to this one TU and never run on a CPU lacking them.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        target_sources(beman.big_int PRIVATE src/ntt_fp_avx2.cpp)
        if(MSVC)
            set_source_files_properties(
                src/ntt_fp_avx2.cpp
                PROPERTIES COMPILE_OPTIONS "/arch:AVX2;${_bigint_fp_strict}"
            )
        else()
            set_source_files_properties(
                src/ntt_fp_avx2.cpp
                PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;${_bigint_fp_strict}"
            )
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        target_sources(beman.big_int PRIVATE src/ntt_fp_neon.cpp)
        set_source_files_properties(
            src/ntt_fp_neon.cpp
            PROPERTIES COMPILE_OPTIONS "${_bigint_fp_strict}"
        )
    endif()
else()
    # Default: the integer Montgomery NTT. No floating-point-environment flags.
    target_sources(beman.big_int PRIVATE src/ntt.cpp src/fft_mul.cpp)
endif()

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
