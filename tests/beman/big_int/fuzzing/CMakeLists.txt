# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(
        FATAL_ERROR
        "BEMAN_BIG_INT_FUZZ requires LLVM Clang (LibFuzzer is not bundled with AppleClang/GCC/MSVC); current compiler is ${CMAKE_CXX_COMPILER_ID}"
    )
endif()

# TODO(mborland) : Add div once div actually passes
set(_fuzz_ops add sub mul)

foreach(op IN LISTS _fuzz_ops)
    set(fuzz_target beman.big_int.fuzz.${op})
    add_executable(${fuzz_target} fuzz_${op}.cpp)
    target_link_libraries(
        ${fuzz_target}
        PRIVATE beman::big_int GTest::gtest Boost::multiprecision
    )
    target_compile_options(
        ${fuzz_target}
        PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g
    )
    target_link_options(
        ${fuzz_target}
        PRIVATE -fsanitize=fuzzer,address,undefined
    )
endforeach()
