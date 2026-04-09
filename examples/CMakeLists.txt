# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(ALL_EXAMPLES todo)
message("Examples to be built: ${ALL_EXAMPLES}")

foreach(example ${ALL_EXAMPLES})
    add_executable(beman.std-big-int.examples.${example})
    target_sources(beman.std-big-int.examples.${example} PRIVATE ${example}.cpp)
    target_link_libraries(
        beman.std-big-int.examples.${example}
        PRIVATE beman::std-big-int
    )
endforeach()
