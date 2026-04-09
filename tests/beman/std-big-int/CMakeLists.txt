# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

find_package(GTest REQUIRED)

add_executable(beman.std-big-int.tests.todo)
target_sources(beman.std-big-int.tests.todo PRIVATE todo.test.cpp)
target_link_libraries(
    beman.std-big-int.tests.todo
    PRIVATE beman::std-big-int GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(beman.std-big-int.tests.todo)
