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
// See addition.test.cpp for why we compare big_int-to-big_int in consteval tests.

consteval bool ce_zero_minus_zero() { return (big_int{0} - big_int{0}) == big_int{0}; }
static_assert(ce_zero_minus_zero());

consteval bool ce_small_positive() { return (big_int{5} - big_int{3}) == big_int{2}; }
static_assert(ce_small_positive());

consteval bool ce_small_negative_result() { return (big_int{3} - big_int{5}) == big_int{-2}; }
static_assert(ce_small_negative_result());

consteval bool ce_cancel_to_zero() { return (big_int{7} - big_int{7}) == big_int{0}; }
static_assert(ce_cancel_to_zero());

consteval bool ce_negative_minus_negative() { return (big_int{-5} - big_int{-3}) == big_int{-2}; }
static_assert(ce_negative_minus_negative());

consteval bool ce_negative_minus_positive() { return (big_int{-5} - big_int{3}) == big_int{-8}; }
static_assert(ce_negative_minus_positive());

consteval bool ce_positive_minus_negative() { return (big_int{5} - big_int{-3}) == big_int{8}; }
static_assert(ce_positive_minus_negative());

// ----- runtime tests -----

TEST(Subtraction, SmallPositive) {
    EXPECT_EQ(big_int{5} - big_int{3}, 2);
    EXPECT_EQ(big_int{42} - big_int{40}, 2);
    EXPECT_EQ(big_int{3} - big_int{5}, -2);
}

TEST(Subtraction, SmallNegative) {
    EXPECT_EQ(big_int{-5} - big_int{-3}, -2);
    EXPECT_EQ(big_int{-3} - big_int{-5}, 2);
}

TEST(Subtraction, MixedSigns) {
    EXPECT_EQ(big_int{5} - big_int{-3}, 8);
    EXPECT_EQ(big_int{-5} - big_int{3}, -8);
    EXPECT_EQ(big_int{3} - big_int{-5}, 8);
    EXPECT_EQ(big_int{-3} - big_int{5}, -8);
}

TEST(Subtraction, CancelToExactZeroIsPositive) {
    // x - x must produce +0, never -0.
    const big_int r = big_int{7} - big_int{7};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);

    const big_int r2 = big_int{-100} - big_int{-100};
    EXPECT_EQ(r2, 0);
    EXPECT_FALSE(r2 < 0);
}

TEST(Subtraction, ZeroIdentity) {
    EXPECT_EQ(big_int{0} - big_int{0}, 0);
    EXPECT_EQ(big_int{42} - big_int{0}, 42);
    EXPECT_EQ(big_int{0} - big_int{42}, -42);
    EXPECT_EQ(big_int{-42} - big_int{0}, -42);
    EXPECT_EQ(big_int{0} - big_int{-42}, 42);
}

TEST(Subtraction, BorrowAcrossLimbPromotesToHeap) {
    // 0 - UINT64_MAX requires two limbs' worth of magnitude? No — the magnitude
    // UINT64_MAX fits in one limb. But `(UINT64_MAX + 1) - 1` goes through the
    // carry path and shrinks back. Here we go the opposite way: pick two values
    // whose sum of magnitudes exceeds a single limb.
    const big_int a{std::numeric_limits<std::uint64_t>::max()};
    const big_int b{-1};
    // a - b = a + 1 = 2^64 → two-limb result, must promote to heap.
    const big_int r = a - b;
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});
    EXPECT_GT(r.capacity(), 0u);
}

TEST(Subtraction, NoAllocationWhenInlineFits) {
    using big_int_256 = basic_big_int<256>;
    const big_int_256 a{std::numeric_limits<std::uint64_t>::max()};
    const big_int_256 b{-1};
    const big_int_256 r = a - b; // 2^64 — still fits inline in a 256-bit inline buffer.
    EXPECT_EQ(r.capacity(), 0u);
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});
}

TEST(Subtraction, MultiLimbSameSignTrimsLeadingZeros) {
    // 2^64 - 1 = UINT64_MAX → must trim back to one limb.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 - big_int{1};
    ASSERT_EQ(r.representation().size(), 1u);
    EXPECT_EQ(r.representation()[0], std::numeric_limits<std::uint64_t>::max());
}

TEST(Subtraction, MultiLimbCancelsToZero) {
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 - two_64;
    ASSERT_EQ(r.representation().size(), 1u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Subtraction, MultiLimbSameSignCrossesLimbBoundary) {
    // 2^65 - 2^64 = 2^64, spanning a limb transition on the larger side.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int two_65 = two_64 + two_64;
    const big_int r      = two_65 - two_64;
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});
}

TEST(Subtraction, BigIntMinusPrimitiveUnsigned) {
    EXPECT_EQ(big_int{10} - 5U, 5);
    EXPECT_EQ(big_int{5} - 10U, -5);
    EXPECT_EQ(big_int{-10} - 5U, -15);
    EXPECT_EQ(big_int{10} - 0U, 10);
}

TEST(Subtraction, PrimitiveUnsignedMinusBigInt) {
    EXPECT_EQ(10U - big_int{5}, 5);
    EXPECT_EQ(5U - big_int{10}, -5);
    EXPECT_EQ(5U - big_int{-10}, 15);
    EXPECT_EQ(0U - big_int{10}, -10);
}

TEST(Subtraction, BigIntMinusPrimitiveSigned) {
    EXPECT_EQ(big_int{10} - 5, 5);
    EXPECT_EQ(big_int{10} - -5, 15);
    EXPECT_EQ(big_int{-10} - 5, -15);
    EXPECT_EQ(big_int{-10} - -5, -5);
    EXPECT_EQ(big_int{5} - 5, 0);
}

TEST(Subtraction, PrimitiveSignedMinusBigInt) {
    EXPECT_EQ(5 - big_int{10}, -5);
    EXPECT_EQ(-5 - big_int{10}, -15);
    EXPECT_EQ(5 - big_int{-10}, 15);
    EXPECT_EQ(-5 - big_int{-10}, 5);
    EXPECT_EQ(5 - big_int{5}, 0);
}

TEST(Subtraction, PrimitiveBorrowAcrossLimb) {
    // big_int{UINT64_MAX} - (-1) = 2^64 via primitive rhs.
    const big_int r = big_int{std::numeric_limits<std::uint64_t>::max()} - -1;
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});
}

TEST(Subtraction, UnequalLengthMultiLimb) {
    // (2^64 + 10) - 3 = 2^64 + 7: long minus short, same sign, no trim.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int a      = two_64 + big_int{10};
    const big_int r      = a - big_int{3};
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{7});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});

    // (2^64 + 10) - (-3) = 2^64 + 13: long minus negative short, same-sign add path.
    const big_int r2 = a - big_int{-3};
    ASSERT_EQ(r2.representation().size(), 2u);
    EXPECT_EQ(r2.representation()[0], uint_multiprecision_t{13});
    EXPECT_EQ(r2.representation()[1], uint_multiprecision_t{1});
}

TEST(Subtraction, AgreesWithAdditionOfNegated) {
    // Property: (a - b) == (a + (-b)) for a variety of inputs.
    const big_int cases[] = {
        big_int{0},
        big_int{1},
        big_int{-1},
        big_int{std::numeric_limits<std::uint64_t>::max()},
        big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1},
        -(big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}),
    };
    for (const auto& a : cases) {
        for (const auto& b : cases) {
            EXPECT_EQ(a - b, a + (-b));
        }
    }
}

} // namespace
