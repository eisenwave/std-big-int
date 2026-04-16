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

consteval bool ce_zero_times_zero() { return (big_int{0} * big_int{0}) == big_int{0}; }
static_assert(ce_zero_times_zero());

consteval bool ce_small_positive() { return (big_int{3} * big_int{7}) == big_int{21}; }
static_assert(ce_small_positive());

consteval bool ce_identity() { return (big_int{42} * big_int{1}) == big_int{42}; }
static_assert(ce_identity());

consteval bool ce_negative_identity() { return (big_int{42} * big_int{-1}) == big_int{-42}; }
static_assert(ce_negative_identity());

consteval bool ce_both_negative() { return (big_int{-3} * big_int{-7}) == big_int{21}; }
static_assert(ce_both_negative());

consteval bool ce_mixed_sign() { return (big_int{-3} * big_int{7}) == big_int{-21}; }
static_assert(ce_mixed_sign());

consteval bool ce_zero_absorbs() { return (big_int{12345} * big_int{0}) == big_int{0}; }
static_assert(ce_zero_absorbs());

// ----- runtime tests -----

TEST(Multiplication, SmallPositivePositive) {
    EXPECT_EQ(big_int{2} * big_int{3}, 6);
    EXPECT_EQ(big_int{7} * big_int{11}, 77);
    EXPECT_EQ(big_int{100} * big_int{200}, 20000);
}

TEST(Multiplication, SmallNegativeNegative) {
    EXPECT_EQ(big_int{-2} * big_int{-3}, 6);
    EXPECT_EQ(big_int{-7} * big_int{-11}, 77);
}

TEST(Multiplication, MixedSigns) {
    EXPECT_EQ(big_int{-5} * big_int{3}, -15);
    EXPECT_EQ(big_int{5} * big_int{-3}, -15);
    EXPECT_EQ(big_int{-1} * big_int{100}, -100);
    EXPECT_EQ(big_int{100} * big_int{-1}, -100);
}

TEST(Multiplication, ZeroIdentity) {
    EXPECT_EQ(big_int{0} * big_int{0}, 0);
    EXPECT_EQ(big_int{42} * big_int{0}, 0);
    EXPECT_EQ(big_int{0} * big_int{42}, 0);
    EXPECT_EQ(big_int{-42} * big_int{0}, 0);
    EXPECT_EQ(big_int{0} * big_int{-42}, 0);

    // No negative zero from negative * zero
    const big_int r = big_int{-42} * big_int{0};
    EXPECT_EQ(r, 0);
    EXPECT_FALSE(r < 0);
}

TEST(Multiplication, MultiplicativeIdentity) {
    EXPECT_EQ(big_int{42} * big_int{1}, 42);
    EXPECT_EQ(big_int{1} * big_int{42}, 42);
    EXPECT_EQ(big_int{-42} * big_int{1}, -42);
    EXPECT_EQ(big_int{1} * big_int{-42}, -42);
}

TEST(Multiplication, SingleLimbOverflow) {
    // UINT64_MAX * 2 should produce a two-limb result.
    const auto    max_val = std::numeric_limits<std::uint64_t>::max();
    const big_int a{max_val};
    const big_int b{2};
    const big_int r = a * b;
    // max_val * 2 = 2^65 - 2 = [0xFFFFFFFFFFFFFFFE, 1]
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], max_val - 1);
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{1});
}

TEST(Multiplication, SingleLimbTimesMultiLimb) {
    // (2^64) * 3 = 3 * 2^64
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 * big_int{3};
    // 3 * 2^64 = [0, 3]
    ASSERT_EQ(r.representation().size(), 2u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{3});
}

TEST(Multiplication, MultiLimbTimesMultiLimb) {
    // (2^64) * (2^64) = 2^128
    const big_int two_64 = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};
    const big_int r      = two_64 * two_64;
    // 2^128 = [0, 0, 1] in 64-bit limbs
    ASSERT_EQ(r.representation().size(), 3u);
    EXPECT_EQ(r.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[1], uint_multiprecision_t{0});
    EXPECT_EQ(r.representation()[2], uint_multiprecision_t{1});
}

TEST(Multiplication, SelfMultiplication) {
    // 1000 * 1000 = 1000000
    const big_int a{1000};
    const big_int r = a * a;
    EXPECT_EQ(r, 1000000);
}

TEST(Multiplication, Commutativity) {
    const big_int a{123456789};
    const big_int b{987654321};
    EXPECT_EQ(a * b, b * a);
}

TEST(Multiplication, PowerOfTwoMatchesShift) {
    // x * 2^k should equal x << k
    const big_int x{12345};
    const big_int two = big_int{1} << 10; // 1024
    EXPECT_EQ(x * two, x << 10);
}

TEST(Multiplication, BigIntTimesPrimitiveUnsigned) {
    EXPECT_EQ(big_int{10} * 5U, 50);
    EXPECT_EQ(big_int{-10} * 5U, -50);
    EXPECT_EQ(big_int{0} * 5U, 0);
    EXPECT_EQ(big_int{10} * 0U, 0);
}

TEST(Multiplication, PrimitiveUnsignedTimesBigInt) {
    EXPECT_EQ(5U * big_int{10}, 50);
    EXPECT_EQ(5U * big_int{-10}, -50);
    EXPECT_EQ(0U * big_int{10}, 0);
}

TEST(Multiplication, BigIntTimesPrimitiveSigned) {
    EXPECT_EQ(big_int{10} * 5, 50);
    EXPECT_EQ(big_int{10} * -5, -50);
    EXPECT_EQ(big_int{-10} * 5, -50);
    EXPECT_EQ(big_int{-10} * -5, 50);
    EXPECT_EQ(big_int{10} * 0, 0);
}

TEST(Multiplication, PrimitiveSignedTimesBigInt) {
    EXPECT_EQ(5 * big_int{10}, 50);
    EXPECT_EQ(-5 * big_int{10}, -50);
    EXPECT_EQ(5 * big_int{-10}, -50);
    EXPECT_EQ(-5 * big_int{-10}, 50);
}

TEST(Multiplication, CompoundAssignmentBasic) {
    big_int a{6};
    a *= big_int{7};
    EXPECT_EQ(a, 42);
}

TEST(Multiplication, CompoundAssignmentPrimitive) {
    big_int a{6};
    a *= 7;
    EXPECT_EQ(a, 42);

    big_int b{-10};
    b *= -3;
    EXPECT_EQ(b, 30);
}

TEST(Multiplication, CompoundAssignmentZero) {
    big_int a{42};
    a *= big_int{0};
    EXPECT_EQ(a, 0);
    EXPECT_FALSE(a < 0);
}

TEST(Multiplication, CompoundAssignmentMultiLimb) {
    big_int a = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1}; // 2^64
    a *= big_int{2};
    // 2^65 = [0, 2]
    ASSERT_EQ(a.representation().size(), 2u);
    EXPECT_EQ(a.representation()[0], uint_multiprecision_t{0});
    EXPECT_EQ(a.representation()[1], uint_multiprecision_t{2});
}

TEST(Multiplication, LargeMultiLimbLongMul) {
    // Construct numbers that span several limbs but stay below the Karatsuba cutoff.
    // a = 2^256 - 1 (4 limbs of all 1s on 64-bit)
    big_int a{1};
    a <<= 256;
    a = a + big_int{-1}; // 2^256 - 1

    // b = 2
    // a * 2 = 2^257 - 2
    const big_int r        = a * big_int{2};
    const big_int expected = (big_int{1} << 257) + big_int{-2};
    EXPECT_EQ(r, expected);
}

TEST(Multiplication, FactorialSmoke) {
    // Compute 20! and verify.
    // 20! = 2432902008176640000
    big_int factorial{1};
    for (int i = 2; i <= 20; ++i) {
        factorial *= i;
    }
    // Verify against known value: 20! = 2432902008176640000
    const big_int expected{static_cast<std::uint64_t>(2432902008176640000ULL)};
    EXPECT_EQ(factorial, expected);
}

TEST(Multiplication, LargeFactorial) {
    // Compute 50! which requires multiple limbs.
    // 50! = 30414093201713378043612608166979581188299763898377856820553615673507270386838265
    // We verify by checking a * b = b * a consistency and that the result has the right number
    // of limbs (50! has about 214 bits, so 4 limbs on 64-bit).
    big_int factorial{1};
    for (int i = 2; i <= 50; ++i) {
        factorial *= i;
    }
    EXPECT_GT(factorial.representation().size(), 2u);
    EXPECT_TRUE(factorial > 0);

    // Cross-check: compute in reverse order and verify equality.
    big_int factorial_rev{1};
    for (int i = 50; i >= 2; --i) {
        factorial_rev *= i;
    }
    EXPECT_EQ(factorial, factorial_rev);
}

TEST(Multiplication, MultiLimbSquaring) {
    // (2^128 - 1)^2 = 2^256 - 2^129 + 1
    big_int a{1};
    a <<= 128;
    a = a + big_int{-1}; // 2^128 - 1

    const big_int r        = a * a;
    const big_int expected = (big_int{1} << 256) + (-(big_int{1} << 129)) + big_int{1};
    EXPECT_EQ(r, expected);
}

} // namespace
