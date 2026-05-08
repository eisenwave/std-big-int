# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

file(GLOB ALL_EXAMPLE_SOURCES CONFIGURE_DEPENDS "*.cpp")
message("Examples to be built: ${ALL_EXAMPLE_SOURCES}")

foreach(example_source ${ALL_EXAMPLE_SOURCES})
    get_filename_component(example ${example_source} NAME_WE)
    set(example_target beman.big_int.examples.${example})

    add_executable(${example_target})
    target_sources(${example_target} PRIVATE ${example_source})
    target_link_libraries(${example_target} PRIVATE beman::big_int)

    if(BEMAN_BIG_INT_BUILD_TESTS)
        if(skip_slow_examples AND example IN_LIST SLOW_EXAMPLES)
            message(
                STATUS
                "Skipping slow example from ctest under Debug/sanitizer build: ${example}"
            )
        else()
            add_test(NAME ${example_target} COMMAND ${example_target})
        endif()
    endif()
endforeach()
