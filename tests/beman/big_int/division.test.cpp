// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

namespace {

using beman::big_int::basic_big_int;
using beman::big_int::big_int;
using beman::big_int::uint_multiprecision_t;

// ----- compile-time sanity -----

consteval bool ce_zero_by_one() { return (big_int{0} / big_int{1}) == big_int{0}; }
static_assert(ce_zero_by_one());

consteval bool ce_small_positive() { return (big_int{21} / big_int{7}) == big_int{3}; }
static_assert(ce_small_positive());

consteval bool ce_exact_multiple() { return (big_int{42} / big_int{6}) == big_int{7}; }
static_assert(ce_exact_multiple());

consteval bool ce_truncation_positive() { return (big_int{7} / big_int{3}) == big_int{2}; }
static_assert(ce_truncation_positive());

consteval bool ce_truncation_negative_dividend() { return (big_int{-7} / big_int{3}) == big_int{-2}; }
static_assert(ce_truncation_negative_dividend());

consteval bool ce_truncation_negative_divisor() { return (big_int{7} / big_int{-3}) == big_int{-2}; }
static_assert(ce_truncation_negative_divisor());

consteval bool ce_truncation_both_negative() { return (big_int{-7} / big_int{-3}) == big_int{2}; }
static_assert(ce_truncation_both_negative());

consteval bool ce_dividend_less_than_divisor() { return (big_int{3} / big_int{7}) == big_int{0}; }
static_assert(ce_dividend_less_than_divisor());

consteval bool ce_self_division() { return (big_int{42} / big_int{42}) == big_int{1}; }
static_assert(ce_self_division());

// ----- runtime tests -----

TEST(Division, SmallPositivePositive) {
    EXPECT_EQ(big_int{42} / big_int{7}, 6);
    EXPECT_EQ(big_int{100} / big_int{3}, 33);
    EXPECT_EQ(big_int{1} / big_int{1}, 1);
}

TEST(Division, ExactMultiples) {
    EXPECT_EQ(big_int{0} / big_int{5}, 0);
    EXPECT_EQ(big_int{10} / big_int{2}, 5);
    EXPECT_EQ(big_int{999} / big_int{3}, 333);
}

TEST(Division, TruncationTowardZero) {
    // C++ truncated division: quotient rounds toward zero.
    EXPECT_EQ(big_int{7} / big_int{3}, 2);
    EXPECT_EQ(big_int{-7} / big_int{3}, -2);
    EXPECT_EQ(big_int{7} / big_int{-3}, -2);
    EXPECT_EQ(big_int{-7} / big_int{-3}, 2);
}

TEST(Division, MixedSigns) {
    EXPECT_EQ(big_int{-42} / big_int{7}, -6);
    EXPECT_EQ(big_int{42} / big_int{-7}, -6);
    EXPECT_EQ(big_int{-42} / big_int{-7}, 6);
}

TEST(Division, DividendLessThanDivisor) {
    EXPECT_EQ(big_int{3} / big_int{7}, 0);
    EXPECT_EQ(big_int{-3} / big_int{7}, 0);
    EXPECT_EQ(big_int{3} / big_int{-7}, 0);

    // No negative zero from sign combination.
    const big_int r = big_int{-3} / big_int{7};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Division, ZeroDividend) {
    EXPECT_EQ(big_int{0} / big_int{5}, 0);
    EXPECT_EQ(big_int{0} / big_int{-5}, 0);
    EXPECT_EQ(big_int{0} / big_int{9999999}, 0);
}

TEST(Division, DividendEqualsDivisor) {
    EXPECT_EQ(big_int{42} / big_int{42}, 1);
    EXPECT_EQ(big_int{-42} / big_int{42}, -1);
    EXPECT_EQ(big_int{42} / big_int{-42}, -1);
    EXPECT_EQ(big_int{-42} / big_int{-42}, 1);
}

TEST(Division, IdentityByOne) {
    EXPECT_EQ(big_int{42} / big_int{1}, 42);
    EXPECT_EQ(big_int{-42} / big_int{1}, -42);
    EXPECT_EQ(big_int{42} / big_int{-1}, -42);
    EXPECT_EQ(big_int{-42} / big_int{-1}, 42);
}

TEST(Division, MultiLimbBySingleLimb) {
    // (2^64) / 2 == 2^63
    const big_int two_64   = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r        = two_64 / big_int{2};
    const big_int expected = big_int{static_cast<std::uint64_t>(1) << 63};
    EXPECT_EQ(r, expected);
}

TEST(Division, MultiLimbByMultiLimb) {
    // ((2^64) + 5) / ((2^64) + 1) == 1, remainder 4.
    const big_int two_64_plus_5 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{6};
    const big_int two_64_plus_1 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{2};
    EXPECT_EQ(two_64_plus_5 / two_64_plus_1, 1);
}

TEST(Division, DivideMultiLimbExact) {
    // (a * b) / b == a for randomly constructed big operands.
    const big_int a       = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{123};
    const big_int b       = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{7};
    const big_int product = a * b;
    EXPECT_EQ(product / b, a);
    EXPECT_EQ(product / a, b);
}

TEST(Division, MultiplyAndDivideRoundTrip) {
    // (x * y) / y == x for a sequence of pairs.
    for (int i = 1; i < 50; ++i) {
        for (int j = 1; j < 50; ++j) {
            const big_int x{i * 1'000'000'007LL};
            const big_int y{j * 99'991LL};
            EXPECT_EQ((x * y) / y, x);
        }
    }
}

TEST(Division, LargeShiftBasis) {
    // (2^200) / (2^100) == 2^100
    const big_int dividend = big_int{1} << 200;
    const big_int divisor  = big_int{1} << 100;
    const big_int expected = big_int{1} << 100;
    EXPECT_EQ(dividend / divisor, expected);
}

TEST(Division, CompoundAssignmentBigInt) {
    big_int a{42};
    a /= big_int{7};
    EXPECT_EQ(a, 6);

    big_int b{-42};
    b /= big_int{7};
    EXPECT_EQ(b, -6);
}

TEST(Division, CompoundAssignmentPrimitive) {
    big_int a{100};
    a /= 7;
    EXPECT_EQ(a, 14);

    big_int b{-100};
    b /= 3;
    EXPECT_EQ(b, -33);

    big_int c{100};
    c /= -3;
    EXPECT_EQ(c, -33);
}

TEST(Division, CompoundAssignmentMultiLimb) {
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    a /= big_int{4};
    const big_int expected = big_int{static_cast<std::uint64_t>(1) << 62};
    EXPECT_EQ(a, expected);
}

TEST(Division, SelfDivision) {
    // Exercises the move-into-temp aliasing guard in operator/=.
    big_int a{123456789};
    a /= a;
    EXPECT_EQ(a, 1);

    big_int b{-123456789};
    b /= b;
    EXPECT_EQ(b, 1);
}

TEST(Division, PrimitiveRhsSingleLimbFastPath) {
    // Large dividend, small primitive divisor — hits divmod_into_short.
    const big_int huge = (big_int{1} << 256) + big_int{12345};
    const big_int r    = huge / 7U;
    // Verify: r * 7 + (huge % 7) == huge
    EXPECT_EQ(r * big_int{7} + (huge - r * big_int{7}), huge);
}

TEST(Division, PrimitiveLhs) {
    EXPECT_EQ(42 / big_int{7}, 6);
    EXPECT_EQ(-42 / big_int{7}, -6);
    EXPECT_EQ(42 / big_int{-7}, -6);
}

TEST(Division, HeapPromotionBoundary) {
    // Value well above default inplace_capacity (64 bits * 4 limbs).
    // Using ~320 bits of dividend to guarantee heap storage.
    big_int dividend{1};
    dividend <<= 320;
    dividend = dividend + big_int{12345};
    const big_int divisor{65537};

    const big_int q = dividend / divisor;
    // Verify round-trip: q * divisor + (dividend - q * divisor) == dividend
    const big_int remainder = dividend - q * divisor;
    EXPECT_EQ(q * divisor + remainder, dividend);
    EXPECT_TRUE(remainder >= big_int{0});
    EXPECT_TRUE(remainder < divisor);
}

} // namespace
