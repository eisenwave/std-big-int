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
// Note: we compare big_int-to-big_int rather than big_int-to-primitive to avoid
// the `_BitInt` bit_cast path that some toolchains don't yet support in constant
// evaluation. Sign / storage-promotion properties are covered by the runtime tests.

consteval bool ce_zero_plus_zero() { return (big_int{0} + big_int{0}) == big_int{0}; }
static_assert(ce_zero_plus_zero());

consteval bool ce_small_positive() { return (big_int{1} + big_int{2}) == big_int{3}; }
static_assert(ce_small_positive());

consteval bool ce_cancel_to_zero() { return (big_int{-5} + big_int{5}) == big_int{0}; }
static_assert(ce_cancel_to_zero());

consteval bool ce_mixed_sign_positive_result() { return (big_int{5} + big_int{-3}) == big_int{2}; }
static_assert(ce_mixed_sign_positive_result());

consteval bool ce_mixed_sign_negative_result() {
    return (big_int{-5} + big_int{3}) == big_int{-2};
}
static_assert(ce_mixed_sign_negative_result());

consteval bool ce_same_sign_negative() { return (big_int{-5} + big_int{-3}) == big_int{-8}; }
static_assert(ce_same_sign_negative());

// ----- runtime tests -----

TEST(Addition, SmallPositivePositive) {
    EXPECT_EQ(big_int{1} + big_int{2}, 3);
    EXPECT_EQ(big_int{40} + big_int{2}, 42);
}

TEST(Addition, SmallNegativeNegative) {
    EXPECT_EQ(big_int{-1} + big_int{-2}, -3);
    EXPECT_EQ(big_int{-40} + big_int{-2}, -42);
}

TEST(Addition, MixedSigns) {
    EXPECT_EQ(big_int{-5} + big_int{3}, -2);
    EXPECT_EQ(big_int{5} + big_int{-3}, 2);
    EXPECT_EQ(big_int{3} + big_int{-5}, -2);
    EXPECT_EQ(big_int{-3} + big_int{5}, 2);
}

TEST(Addition, CancelToExactZeroIsPositive) {
    // The "no negative zero" invariant must hold: -a + a must be +0.
    const big_int r = big_int{-7} + big_int{7};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);

    const big_int r2 = big_int{100} + big_int{-100};
    EXPECT_EQ(r2, 0);
    EXPECT_FALSE(r2 < 0);
}

TEST(Addition, ZeroIdentity) {
    EXPECT_EQ(big_int{0} + big_int{0}, 0);
    EXPECT_EQ(big_int{42} + big_int{0}, 42);
    EXPECT_EQ(big_int{0} + big_int{42}, 42);
    EXPECT_EQ(big_int{-42} + big_int{0}, -42);
    EXPECT_EQ(big_int{0} + big_int{-42}, -42);
}

TEST(Addition, CarryOutOfTopLimbPromotesToHeap) {
    // `basic_big_int<64>` with a 64-bit limb has exactly one inline limb.
    // UINT64_MAX + 1 requires two limbs, so the result must live on the heap.
    const big_int a{std::numeric_limits<std::uint64_t>::max()};
    const big_int b{1};
    ASSERT_TRUE(a.representation().size() == 1);
    const big_int r = a + b;
    // Value is 2^64.
    EXPECT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});
    EXPECT_FALSE(r < 0);
    // Storage should have promoted out of inline.
    EXPECT_GT(r.capacity(), 0u);
}

TEST(Addition, NoAllocationWhenInlineFits) {
    // `basic_big_int<256>` has at least 4 inline limbs on any supported limb width.
    // Small-value sum must remain in inline storage (capacity() == 0).
    using big_int_256 = basic_big_int<256>;
    const big_int_256 a{1};
    const big_int_256 b{2};
    const big_int_256 r = a + b;
    EXPECT_EQ(r, 3);
    EXPECT_EQ(r.capacity(), 0u);

    // Even with both operands near the inline limit (but no carry out of
    // the top inline limb), we should not allocate.
    const big_int_256 c{std::numeric_limits<std::uint64_t>::max()};
    const big_int_256 d{1};
    const big_int_256 s = c + d;
    // Value is 2^64, which fits in <= 4 inline limbs → no allocation.
    EXPECT_EQ(s.capacity(), 0u);
    EXPECT_EQ(s.representation().size(), 2u);
    EXPECT_EQ(s.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(s.representation()[1], uint_multiprecision_t{1});
}

TEST(Addition, MultiLimbValuesSameSign) {
    // Build 2^64 via UINT64_MAX + 1, then add another 2^64 to reach 2^65.
    const big_int two_64     = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int two_64_alt = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int two_65     = two_64 + two_64_alt;
    // 2^65 = [0, 2] in a 64-bit-limb representation.
    ASSERT_EQ(two_65.representation().size(), 2u);
    EXPECT_EQ(two_65.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(two_65.representation()[1], uint_multiprecision_t{2});
}

TEST(Addition, MultiLimbOppositeSignTrimsLeadingZeros) {
    // 2^64 + (-1) = 2^64 - 1 = UINT64_MAX, which fits in a single limb.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    ASSERT_EQ(two_64.representation().size(), 2u);

    const big_int r = two_64 + big_int{-1};
    // The subtraction path must trim the now-zero upper limb.
    ASSERT_EQ(r.representation().size(), 1u);
    EXPECT_EQ(r.representation()[0], std::numeric_limits<std::uint64_t>::max());
    EXPECT_FALSE(r < 0);
}

TEST(Addition, MultiLimbSubtractionToZero) {
    // 2^64 + (-2^64) = 0 → must produce +0 with limb_count == 1.
    const big_int two_64     = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int neg_two_64 = -two_64;
    const big_int r          = two_64 + neg_two_64;
    ASSERT_EQ(r.representation().size(), 1u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Addition, BigIntPlusPrimitiveUnsigned) {
    EXPECT_EQ(big_int{10} + 5U, 15);
    EXPECT_EQ(big_int{-10} + 5U, -5);
    EXPECT_EQ(big_int{-10} + 20U, 10);
    EXPECT_EQ(big_int{10} + 0U, 10);
}

TEST(Addition, PrimitiveUnsignedPlusBigInt) {
    EXPECT_EQ(5U + big_int{10}, 15);
    EXPECT_EQ(5U + big_int{-10}, -5);
    EXPECT_EQ(20U + big_int{-10}, 10);
    EXPECT_EQ(0U + big_int{10}, 10);
}

TEST(Addition, BigIntPlusPrimitiveSigned) {
    EXPECT_EQ(big_int{10} + 5, 15);
    EXPECT_EQ(big_int{10} + -5, 5);
    EXPECT_EQ(big_int{-10} + 5, -5);
    EXPECT_EQ(big_int{-10} + -5, -15);
    EXPECT_EQ(big_int{5} + -5, 0);
}

TEST(Addition, PrimitiveSignedPlusBigInt) {
    EXPECT_EQ(5 + big_int{10}, 15);
    EXPECT_EQ(-5 + big_int{10}, 5);
    EXPECT_EQ(5 + big_int{-10}, -5);
    EXPECT_EQ(-5 + big_int{-10}, -15);
    EXPECT_EQ(-5 + big_int{5}, 0);
}

TEST(Addition, PrimitiveCarryIntoUpperLimb) {
    // big_int{UINT64_MAX} + 1U must promote to two limbs.
    const big_int r = big_int{std::numeric_limits<std::uint64_t>::max()} + 1U;
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});
}

TEST(Addition, NegativePrimitiveIntoMultiLimb) {
    // 2^64 + (-1) via primitive operand.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 + -1;
    ASSERT_EQ(r.representation().size(), 1u);
    EXPECT_EQ(r.representation()[0], std::numeric_limits<std::uint64_t>::max());
}

TEST(Addition, UnequalLengthMultiLimb) {
    // (2^64 + 7) + 3 = 2^64 + 10, spanning two limbs on the left and one on the right.
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int a      = two_64 + big_int{7};
    const big_int r      = a + big_int{3};
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{10});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});

    // Same but with a negative short operand: (2^64 + 7) + (-3) = 2^64 + 4.
    const big_int r2 = a + big_int{-3};
    ASSERT_EQ(r2.representation().size(), 2u);
    EXPECT_EQ(r2.representation()[0], uint_multiprecision_t{4});
    EXPECT_EQ(r2.representation()[1], uint_multiprecision_t{1});
}

} // namespace
