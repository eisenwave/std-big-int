// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

TEST(Conversion, ToBool) {
    constexpr beman::big_int::big_int zero;
    constexpr beman::big_int::big_int one(1);
    static_assert(!static_cast<bool>(zero));
    static_assert(static_cast<bool>(one));
    EXPECT_FALSE(static_cast<bool>(zero));
    EXPECT_TRUE(static_cast<bool>(one));
    EXPECT_TRUE(static_cast<bool>(beman::big_int::big_int(-42)));
}

TEST(Conversion, ToInt) {
    constexpr beman::big_int::big_int x(42);
    constexpr beman::big_int::big_int neg(-42);
    static_assert(static_cast<int>(x) == 42);
    static_assert(static_cast<int>(neg) == -42);
    EXPECT_EQ(static_cast<int>(x), 42);
    EXPECT_EQ(static_cast<long long>(x), 42LL);
    EXPECT_EQ(static_cast<int>(neg), -42);
    EXPECT_EQ(static_cast<unsigned int>(x), 42U);
}

TEST(Conversion, ToIntTruncates) {
    beman::big_int::big_int x(0x1'0000'0042LL);
    EXPECT_EQ(static_cast<int>(x), 0x42);
}

TEST(Conversion, ToDouble) {
    beman::big_int::big_int x(1000000000000000LL);
    EXPECT_DOUBLE_EQ(static_cast<double>(x), 1e15);
    beman::big_int::big_int neg(-42);
    EXPECT_DOUBLE_EQ(static_cast<double>(neg), -42.0);
}

TEST(Conversion, ToFloat) {
    beman::big_int::big_int x(123);
    EXPECT_FLOAT_EQ(static_cast<float>(x), 123.0f);
}

TEST(Conversion, ToUnsignedWrapsNegative) {
    beman::big_int::big_int neg(-1);
    EXPECT_EQ(static_cast<unsigned int>(neg), std::numeric_limits<unsigned int>::max());
    EXPECT_EQ(static_cast<unsigned long long>(neg), std::numeric_limits<unsigned long long>::max());
}

TEST(Conversion, ToDoubleMultiLimb) {
    beman::big_int::big_int x(static_cast<double>(1ULL << 63) * 4.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(x), static_cast<double>(1ULL << 63) * 4.0);
}

TEST(Conversion, ToFloatLarge) {
    beman::big_int::big_int x(std::numeric_limits<float>::max());
    EXPECT_FLOAT_EQ(static_cast<float>(x), std::numeric_limits<float>::max());
}

TEST(Conversion, ZeroToAllTypes) {
    constexpr beman::big_int::big_int zero;
    static_assert(static_cast<int>(zero) == 0);
    static_assert(static_cast<unsigned int>(zero) == 0U);
    static_assert(static_cast<bool>(zero) == false);
    EXPECT_EQ(static_cast<double>(zero), 0.0);
    EXPECT_EQ(static_cast<float>(zero), 0.0f);
}
