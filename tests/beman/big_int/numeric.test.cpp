// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>
#include <limits>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include <beman/big_int.hpp>

#include "testing.hpp"

namespace {

using beman::big_int::abs;
using beman::big_int::big_int;
using beman::big_int::uint_multiprecision_t;

// `abs` always returns a prvalue of the decayed big_int type, regardless of the
// value category or cv-qualification of the argument.
static_assert(std::is_same_v<decltype(abs(std::declval<big_int>())), big_int>);
static_assert(std::is_same_v<decltype(abs(std::declval<big_int&>())), big_int>);
static_assert(std::is_same_v<decltype(abs(std::declval<const big_int&>())), big_int>);

// The result is usable in a constant expression for in-place (non-allocating) values.
static_assert(abs(big_int{-5}) == 5);
static_assert(abs(big_int{5}) == 5);
static_assert(abs(big_int{0}) == 0);

TEST(Abs, SmallPositive) {
    EXPECT_EQ(abs(big_int{1}), 1);
    EXPECT_EQ(abs(big_int{42}), 42);
    EXPECT_EQ(abs(big_int{std::numeric_limits<uint_multiprecision_t>::max()}),
              std::numeric_limits<uint_multiprecision_t>::max());
}

TEST(Abs, SmallNegative) {
    EXPECT_EQ(abs(big_int{-1}), 1);
    EXPECT_EQ(abs(big_int{-42}), 42);
    EXPECT_EQ((abs(big_int{-42}) <=> 0), std::strong_ordering::greater);
}

TEST(Abs, Zero) {
    const big_int z = abs(big_int{0});
    EXPECT_EQ(z, 0);
    // Never the corrupt negative-zero state: it compares equal to zero, and so
    // does its negation.
    EXPECT_EQ((z <=> 0), std::strong_ordering::equal);
    EXPECT_EQ(-z, 0);
}

TEST(Abs, LargePositiveStaysOnHeap) {
    const big_int x = big_int{1} << 200;
    ASSERT_FALSE(is_inplace(x));

    const big_int a = abs(x);

    EXPECT_EQ(a, x);
    EXPECT_FALSE(is_inplace(a));
    EXPECT_EQ((a <=> 0), std::strong_ordering::greater);
}

TEST(Abs, LargeNegative) {
    const big_int magnitude = big_int{1} << 200;
    const big_int x         = -magnitude;
    ASSERT_FALSE(is_inplace(x));

    const big_int a = abs(x);

    EXPECT_EQ(a, magnitude);
    EXPECT_EQ((a <=> 0), std::strong_ordering::greater);
}

TEST(Abs, CrossesLimbBoundary) {
    const big_int magnitude = big_int{1} << 64;

    EXPECT_EQ(abs(magnitude), magnitude);
    EXPECT_EQ(abs(-magnitude), magnitude);
}

TEST(Abs, LvalueLeavesArgumentUnchanged) {
    big_int x{-42};

    const big_int a = abs(x);

    EXPECT_EQ(a, 42);
    EXPECT_EQ(x, -42); // The lvalue overload copies; the argument is untouched.
}

TEST(Abs, RvalueLargeProducesMagnitude) {
    // An rvalue argument is moved into the result; the heap storage survives.
    const big_int a = abs(-(big_int{1} << 200));

    EXPECT_EQ(a, big_int{1} << 200);
    EXPECT_FALSE(is_inplace(a));
}

TEST(Abs, Idempotent) {
    const big_int positive{1234567};
    const big_int negative{-1234567};

    EXPECT_EQ(abs(abs(negative)), abs(negative));
    EXPECT_EQ(abs(abs(positive)), positive);
}

TEST(Abs, EqualsAbsOfNegation) {
    big_int x{-98765};

    EXPECT_EQ(abs(x), abs(-x));
    EXPECT_EQ(abs(-x), 98765);

    const big_int big = big_int{1} << 150;
    EXPECT_EQ(abs(big), abs(-big));
}

TEST(Abs, CallableFullyQualified) {
    // The function is reachable both via the using-declaration above (ADL) and
    // when named explicitly through its namespace.
    EXPECT_EQ(beman::big_int::abs(big_int{-7}), 7);
}

} // namespace
