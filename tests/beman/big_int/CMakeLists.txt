# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_executable(beman.big_int.tests.todo)
target_sources(beman.big_int.tests.todo PRIVATE todo.test.cpp)
target_link_libraries(
    beman.big_int.tests.todo
    PRIVATE beman::big_int GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(beman.big_int.tests.todo)
