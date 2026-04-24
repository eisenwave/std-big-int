// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <climits>
#include <beman/big_int/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

TEST(FloatConstruction, DoubleZero) {
    beman::big_int::big_int x(0.0);
    EXPECT_EQ(x.width_mag(), 0U);
    EXPECT_EQ(x.representation()[0], 0U);
}

TEST(FloatConstruction, DoublePositive) {
    beman::big_int::big_int x(42.9);
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(FloatConstruction, DoubleNegative) {
    beman::big_int::big_int x(-42.9);
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(FloatConstruction, DoubleFractionalLessThanOne) {
    beman::big_int::big_int x(0.9999);
    EXPECT_EQ(x.representation()[0], 0U);
}

TEST(FloatConstruction, DoubleExact) {
    beman::big_int::big_int x(1e15);
    EXPECT_EQ(x.representation()[0], 1000000000000000ULL);
}

TEST(FloatConstruction, DoubleWithAllocator) {
    std::allocator<beman::big_int::uint_multiprecision_t> a;
    beman::big_int::big_int                               x(42.7, a);
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(FloatConstruction, FloatPositive) {
    beman::big_int::big_int x(123.9f);
    EXPECT_EQ(x.representation()[0], 123U);
}

TEST(FloatConstruction, FloatFractionalLessThanOne) {
    beman::big_int::big_int x(0.5f);
    EXPECT_EQ(x.representation()[0], 0U);
}

TEST(FloatConstruction, LongDoublePositive) {
#ifdef BEMAN_BIG_INT_UNSUPPORTED_LONG_DOUBLE
    GTEST_SKIP() << "long double not supported on this platform";
#else
    beman::big_int::big_int x(42.9L);
    EXPECT_EQ(x.representation()[0], 42U);
#endif
}

TEST(FloatConstruction, DoubleLargeMultiLimb) {
    beman::big_int::big_int x(static_cast<double>(1ULL << 63) * 4.0);
    EXPECT_EQ(x.representation().size(), 2U);
}

TEST(FloatConstruction, DoubleBoundaryFastPath) {
    beman::big_int::big_int x(static_cast<double>(1ULL << 62));
    EXPECT_EQ(x.representation()[0], 1ULL << 62);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(FloatConstruction, DoubleBoundaryGeneralPath) {
    beman::big_int::big_int x(static_cast<double>(1ULL << 63) * 2.0);
    EXPECT_EQ(x.representation().size(), 2U);
}

TEST(FloatConstruction, FloatLarge) {
    beman::big_int::big_int x(std::numeric_limits<float>::max());
    EXPECT_EQ(x.representation().size(), 2U);
}

TEST(FloatConstruction, DoubleSubnormal) {
    beman::big_int::big_int x(5e-324);
    EXPECT_EQ(x.representation()[0], 0U);
    EXPECT_EQ(x.representation().size(), 1U);
}
