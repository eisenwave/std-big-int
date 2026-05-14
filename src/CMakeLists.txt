# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0

target_sources(
    beman.big_int
    PRIVATE
        karatsuba.cpp
        toom_cook_3.cpp
        toom_cook_4.cpp
        toom_cook_6_5.cpp
)
