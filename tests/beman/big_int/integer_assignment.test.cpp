// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

TEST(IntegerAssignment, AssignPositive) {
    beman::big_int::big_int x;
    x = 42;
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(IntegerAssignment, AssignNegative) {
    beman::big_int::big_int x;
    x = -42;
    // TODO(alcxpr): how can we test sign?
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(IntegerAssignment, AssignZero) {
    beman::big_int::big_int x(42);
    x = 0;
    EXPECT_EQ(x.representation()[0], 0U);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(IntegerAssignment, AssignUnsigned) {
    beman::big_int::big_int x;
    x = 1000000000000000ULL;
    EXPECT_EQ(x.representation()[0], 1000000000000000ULL);
}

TEST(IntegerAssignment, AssignOverwritesLargerValue) {
    beman::big_int::big_int x(static_cast<double>(1ULL << 63) * 4.0);
    x = 1;
    EXPECT_EQ(x.representation().size(), 1U);
    EXPECT_EQ(x.representation()[0], 1U);
}

TEST(IntegerAssignment, AssignCrossAllocator) {
    beman::big_int::basic_big_int<256> src(42);
    beman::big_int::big_int            dst;
    dst = src;
    EXPECT_EQ(dst.representation()[0], 42U);
}

TEST(IntegerAssignment, AssignCrossInplaceBitsForcesReallocation) {
    beman::big_int::basic_big_int<64>  x(1);
    beman::big_int::basic_big_int<256> big(static_cast<double>(1ULL << 63) * 4.0); // 2 limbs via ctor
    x = big;
    EXPECT_EQ(x.representation().size(), 2U);
}
