# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

include(GoogleTest)

file(GLOB TEST_SOURCES "*.test.cpp")

foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    set(test_target beman.big_int.tests.${test_name})

    add_executable(${test_target})
    target_sources(${test_target} PRIVATE ${test_source})
    target_link_libraries(
        ${test_target}
        PRIVATE beman::big_int GTest::gtest_main
    )
    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"
    )
        # These options work with both GCC and Clang
        target_compile_options(
            ${test_target}
            PRIVATE
                -Werror
                -Wall
                -Wextra
                -Wsign-conversion
                -Wconversion
                -Wundef
                -Wold-style-cast
                -Wfloat-equal
        )
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC specific options that are a little bit more aggressive
        target_compile_options(
            ${test_target}
            PRIVATE
                -Wundef
                -Wduplicated-branches
                -Wshadow
                -Wextra-semi
                -Wuseless-cast
                -Wpadded
        )
    endif()
    if(MSVC)
        # Use W4 instead of Wall because it includes noise from system headers
        # /WX = -Werror
        target_compile_options(${test_target} PRIVATE /WX /W4)
    endif()
    gtest_discover_tests(${test_target})
endforeach()
