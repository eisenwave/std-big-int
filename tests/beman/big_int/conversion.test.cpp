// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <gtest/gtest.h>
#include <limits>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

// [big.int.conv] compile-time tests

static_assert(!static_cast<bool>(beman::big_int::big_int{}));
static_assert(static_cast<bool>(beman::big_int::big_int{1}));
static_assert(static_cast<bool>(beman::big_int::big_int{-1}));
static_assert(static_cast<bool>(beman::big_int::big_int{42}));

static_assert(static_cast<int>(beman::big_int::big_int{42}) == 42);
static_assert(static_cast<int>(beman::big_int::big_int{-42}) == -42);
static_assert(static_cast<int>(beman::big_int::big_int{0}) == 0);
static_assert(static_cast<long long>(beman::big_int::big_int{1000000000000LL}) == 1000000000000LL);
static_assert(static_cast<long long>(beman::big_int::big_int{-1000000000000LL}) == -1000000000000LL);

static_assert(static_cast<unsigned int>(beman::big_int::big_int{42}) == 42U);
static_assert(static_cast<unsigned int>(beman::big_int::big_int{0}) == 0U);
static_assert(static_cast<unsigned int>(beman::big_int::big_int{-1}) == std::numeric_limits<unsigned int>::max());
static_assert(static_cast<unsigned long long>(beman::big_int::big_int{-1}) ==
              std::numeric_limits<unsigned long long>::max());

static_assert(static_cast<int>(beman::big_int::big_int{0x1'0000'0042LL}) == 0x42);

// TODO(alcxpr): Use `<<` of the operator instead of IILE when implemented.
//                  Currently, only `<<=` and `>>=` are available.

// MSVC
#ifndef BEMAN_BIG_INT_MSVC
BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_CLANG("-Wfloat-equal")
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wfloat-equal")
static_assert([] {
    beman::big_int::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    return static_cast<float>(x);
}() == std::numeric_limits<float>::infinity());

static_assert([] {
    beman::big_int::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    return static_cast<float>(-x);
}() == -std::numeric_limits<float>::infinity());

static_assert([] {
    beman::big_int::big_int x(std::numeric_limits<double>::max());
    x <<= 1;
    return static_cast<double>(x);
}() == std::numeric_limits<double>::infinity());
BEMAN_BIG_INT_DIAGNOSTIC_POP()
#endif

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

TEST(Conversion, ToFloatInfinity) {
    beman::big_int::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    EXPECT_EQ(static_cast<float>(x), std::numeric_limits<float>::infinity());
}

TEST(Conversion, ToNegativeFloatInfinity) {
    beman::big_int::big_int x(std::numeric_limits<float>::max());
    x <<= 1;
    x = -x;
    EXPECT_EQ(static_cast<float>(x), -std::numeric_limits<float>::infinity());
}

TEST(Conversion, ToDoubleInfinity) {
    beman::big_int::big_int x(std::numeric_limits<double>::max());
    x <<= 1;
    EXPECT_EQ(static_cast<double>(x), std::numeric_limits<double>::infinity());
}

#ifdef BEMAN_BIG_INT_HAS_BITINT
TEST(Conversion, InplaceFastPathInt) {
    beman::big_int::big_int x(42);
    beman::big_int::big_int neg(-42);
    ASSERT_EQ(x.capacity(), 0U);
    ASSERT_EQ(neg.capacity(), 0U);
    EXPECT_EQ(static_cast<int>(x), 42);
    EXPECT_EQ(static_cast<int>(neg), -42);
    EXPECT_EQ(static_cast<unsigned long long>(neg), std::numeric_limits<unsigned long long>::max() - 41U);
}

TEST(Conversion, InplaceFastPathFloat) {
    beman::big_int::big_int x(1000000000000000LL);
    ASSERT_EQ(x.capacity(), 0U);
    EXPECT_DOUBLE_EQ(static_cast<double>(x), 1e15);
    EXPECT_FLOAT_EQ(static_cast<float>(x), 1e15f);
}

TEST(Conversion, InplaceFastPathNegativeFloat) {
    beman::big_int::big_int x(-1000000000000000LL);
    ASSERT_EQ(x.capacity(), 0U);
    EXPECT_DOUBLE_EQ(static_cast<double>(x), -1e15);
    EXPECT_FLOAT_EQ(static_cast<float>(x), -1e15f);
}
#endif
