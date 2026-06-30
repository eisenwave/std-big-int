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
using beman::big_int::saturating_cast;
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

// ============================================================================
// saturating_cast
// ============================================================================

// The result type is exactly the requested type `R`.
static_assert(std::is_same_v<decltype(saturating_cast<int>(std::declval<big_int>())), int>);
static_assert(
    std::is_same_v<decltype(saturating_cast<unsigned long long>(std::declval<big_int>())), unsigned long long>);
static_assert(std::is_same_v<decltype(saturating_cast<signed char>(std::declval<const big_int&>())), signed char>);

// `R` is constrained to a signed or unsigned integer type: bool, character
// types, and floating-point types are rejected (SFINAE-friendly).
template <class R>
concept has_saturating_cast = requires(const big_int& x) { saturating_cast<R>(x); };
static_assert(has_saturating_cast<int>);
static_assert(has_saturating_cast<unsigned int>);
static_assert(has_saturating_cast<long long>);
static_assert(!has_saturating_cast<bool>);
static_assert(!has_saturating_cast<char>);
static_assert(!has_saturating_cast<double>);

// In-range values pass through unchanged, in a constant expression.
static_assert(saturating_cast<int>(big_int{0}) == 0);
static_assert(saturating_cast<int>(big_int{42}) == 42);
static_assert(saturating_cast<int>(big_int{-42}) == -42);
static_assert(saturating_cast<unsigned>(big_int{42}) == 42U);

// Boundary values are exactly representable and are returned as-is.
static_assert(saturating_cast<int>(big_int{std::numeric_limits<int>::max()}) == std::numeric_limits<int>::max());
static_assert(saturating_cast<int>(big_int{std::numeric_limits<int>::min()}) == std::numeric_limits<int>::min());

// Out-of-range values clamp to the nearest representable bound.
static_assert(saturating_cast<signed char>(big_int{1000}) == std::numeric_limits<signed char>::max());
static_assert(saturating_cast<signed char>(big_int{-1000}) == std::numeric_limits<signed char>::min());
static_assert(saturating_cast<unsigned char>(big_int{1000}) == std::numeric_limits<unsigned char>::max());
static_assert(saturating_cast<unsigned char>(big_int{-1}) == 0);

TEST(SaturatingCast, InRangeSigned) {
    EXPECT_EQ(saturating_cast<int>(big_int{0}), 0);
    EXPECT_EQ(saturating_cast<int>(big_int{1}), 1);
    EXPECT_EQ(saturating_cast<int>(big_int{-1}), -1);
    EXPECT_EQ(saturating_cast<int>(big_int{1234567}), 1234567);
    EXPECT_EQ(saturating_cast<int>(big_int{-1234567}), -1234567);
    EXPECT_EQ(saturating_cast<long long>(big_int{1000000000000LL}), 1000000000000LL);
    EXPECT_EQ(saturating_cast<long long>(big_int{-1000000000000LL}), -1000000000000LL);
}

TEST(SaturatingCast, InRangeUnsigned) {
    EXPECT_EQ(saturating_cast<unsigned>(big_int{0}), 0U);
    EXPECT_EQ(saturating_cast<unsigned>(big_int{42}), 42U);
    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{1000000000000ULL}), 1000000000000ULL);
}

TEST(SaturatingCast, ExactBoundsSigned) {
    EXPECT_EQ(saturating_cast<int>(big_int{std::numeric_limits<int>::max()}), std::numeric_limits<int>::max());
    EXPECT_EQ(saturating_cast<int>(big_int{std::numeric_limits<int>::min()}), std::numeric_limits<int>::min());
    EXPECT_EQ(saturating_cast<long long>(big_int{std::numeric_limits<long long>::max()}),
              std::numeric_limits<long long>::max());
    EXPECT_EQ(saturating_cast<long long>(big_int{std::numeric_limits<long long>::min()}),
              std::numeric_limits<long long>::min());
}

TEST(SaturatingCast, ExactBoundsUnsigned) {
    EXPECT_EQ(saturating_cast<unsigned>(big_int{std::numeric_limits<unsigned>::max()}),
              std::numeric_limits<unsigned>::max());
    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{std::numeric_limits<unsigned long long>::max()}),
              std::numeric_limits<unsigned long long>::max());
}

TEST(SaturatingCast, ClampsAboveMaxSigned) {
    // One past the maximum clamps to the maximum.
    const big_int just_over = big_int{std::numeric_limits<int>::max()} + 1;
    EXPECT_EQ(saturating_cast<int>(just_over), std::numeric_limits<int>::max());

    EXPECT_EQ(saturating_cast<signed char>(big_int{128}), std::numeric_limits<signed char>::max());
    EXPECT_EQ(saturating_cast<signed char>(big_int{1000}), std::numeric_limits<signed char>::max());
    EXPECT_EQ(saturating_cast<long long>(big_int{1} << 200), std::numeric_limits<long long>::max());
}

TEST(SaturatingCast, ClampsBelowMinSigned) {
    // One below the minimum clamps to the minimum.
    const big_int just_under = big_int{std::numeric_limits<int>::min()} - 1;
    EXPECT_EQ(saturating_cast<int>(just_under), std::numeric_limits<int>::min());

    EXPECT_EQ(saturating_cast<signed char>(big_int{-129}), std::numeric_limits<signed char>::min());
    EXPECT_EQ(saturating_cast<signed char>(big_int{-1000}), std::numeric_limits<signed char>::min());
    EXPECT_EQ(saturating_cast<long long>(-(big_int{1} << 200)), std::numeric_limits<long long>::min());
}

TEST(SaturatingCast, ClampsAboveMaxUnsigned) {
    EXPECT_EQ(saturating_cast<unsigned char>(big_int{256}), std::numeric_limits<unsigned char>::max());
    EXPECT_EQ(saturating_cast<unsigned char>(big_int{1000}), std::numeric_limits<unsigned char>::max());

    const big_int just_over = big_int{std::numeric_limits<unsigned>::max()} + 1;
    EXPECT_EQ(saturating_cast<unsigned>(just_over), std::numeric_limits<unsigned>::max());

    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{1} << 200), std::numeric_limits<unsigned long long>::max());
}

TEST(SaturatingCast, NegativeClampsToZeroUnsigned) {
    // Any negative value is below the minimum of an unsigned type (zero).
    EXPECT_EQ(saturating_cast<unsigned>(big_int{-1}), 0U);
    EXPECT_EQ(saturating_cast<unsigned char>(big_int{-1}), 0);
    EXPECT_EQ(saturating_cast<unsigned long long>(big_int{-1}), 0ULL);
    EXPECT_EQ(saturating_cast<unsigned long long>(-(big_int{1} << 200)), 0ULL);
}

TEST(SaturatingCast, LargeHeapValueInRange) {
    // A value that lives on the heap but still fits the destination type
    // round-trips exactly rather than clamping.
    const big_int x = big_int{1} << 62;
    ASSERT_EQ(saturating_cast<long long>(x), 1LL << 62);

    const big_int big_unsigned = big_int{1} << 63;
    EXPECT_EQ(saturating_cast<unsigned long long>(big_unsigned), 1ULL << 63);
    // ...but the same value overflows a signed long long and clamps.
    EXPECT_EQ(saturating_cast<long long>(big_unsigned), std::numeric_limits<long long>::max());
}

#ifdef BEMAN_BIG_INT_HAS_BITINT
TEST(SaturatingCast, BitPreciseIntegers) {
    // Extended/bit-precise integers (no std::numeric_limits specialization) use
    // the width-derived bounds. Results are widened to standard types so the
    // comparison and any diagnostic printing stay on well-supported types.
    using u12 = bit_uint<12>;
    using s12 = bit_int<12>;

    EXPECT_EQ(static_cast<unsigned>(saturating_cast<u12>(big_int{4095})), 4095U); // 2^12 - 1
    EXPECT_EQ(static_cast<unsigned>(saturating_cast<u12>(big_int{4096})), 4095U); // clamps to max
    EXPECT_EQ(static_cast<unsigned>(saturating_cast<u12>(big_int{-1})), 0U);      // clamps to zero

    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{2047})), 2047);   // 2^11 - 1
    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{2048})), 2047);   // clamps to max
    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{-2048})), -2048); // -2^11
    EXPECT_EQ(static_cast<int>(saturating_cast<s12>(big_int{-2049})), -2048); // clamps to min
}
#endif

} // namespace
