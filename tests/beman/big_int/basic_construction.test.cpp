// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/big_int.hpp>
#include <gtest/gtest.h>

// ----- compile-time tests -----

consteval bool test_default_construction() {
    beman::big_int::big_int x;
    return x.width_mag() == 0 && x.representation().size() == 1;
}
static_assert(test_default_construction());

consteval bool test_allocator_construction() {
    std::allocator<beman::big_int::uint_multiprecision_t> a;
    beman::big_int::big_int                               x(a);
    return x.width_mag() == 0 && x.representation().size() == 1;
}
static_assert(test_allocator_construction());

consteval bool test_copy_construction() {
    beman::big_int::big_int x;
    beman::big_int::big_int y(x);
    return y.width_mag() == 0 && y.representation().size() == 1;
}
static_assert(test_copy_construction());

consteval bool test_move_construction() {
    beman::big_int::big_int x;
    beman::big_int::big_int y(std::move(x));
    return y.width_mag() == 0 && y.representation().size() == 1;
}
static_assert(test_move_construction());

consteval bool test_integral_construction() {
    beman::big_int::big_int x{42U};
    return x.width_mag() == 5 && x.representation()[0] == 42U;
}
static_assert(test_integral_construction());

consteval bool test_integral_construction_with_allocator() {
    std::allocator<beman::big_int::uint_multiprecision_t> a;
    beman::big_int::big_int                               x(42, a);
    return x.width_mag() == 5 && x.representation()[0] == 42U;
}
static_assert(test_integral_construction_with_allocator());

consteval bool test_from_range_construction() {
    std::array<beman::big_int::uint_multiprecision_t, 2> limbs{0xDEADBEEFU, 0xCAFEBABEU};
    beman::big_int::big_int                              x(std::from_range, limbs);
    return x.representation().size() == 2;
}
static_assert(test_from_range_construction());

// ----- runtime tests -----

TEST(BasicConstruction, DefaultConstruction) {
    beman::big_int::big_int x;
    EXPECT_EQ(x.width_mag(), 0U);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(BasicConstruction, AllocatorConstruction) {
    std::allocator<beman::big_int::uint_multiprecision_t> a;
    beman::big_int::big_int                               x(a);
    EXPECT_EQ(x.width_mag(), 0U);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(BasicConstruction, CopyConstruction) {
    beman::big_int::big_int x;
    beman::big_int::big_int y(x);
    EXPECT_EQ(y.width_mag(), 0U);
    EXPECT_EQ(y.representation().size(), 1U);
}

TEST(BasicConstruction, MoveConstruction) {
    beman::big_int::big_int x;
    beman::big_int::big_int y(std::move(x));
    EXPECT_EQ(y.width_mag(), 0U);
    EXPECT_EQ(y.representation().size(), 1U);
}

TEST(BasicConstruction, NonDefaultInplaceBits) {
    beman::big_int::basic_big_int<256> x;
    EXPECT_EQ(x.width_mag(), 0U);
    EXPECT_EQ(x.representation().size(), 1U);
}

TEST(BasicConstruction, IntegralConstruction) {
    beman::big_int::big_int x(42);
    EXPECT_EQ(x.width_mag(), 5U);
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(BasicConstruction, IntegralConstructionNegative) {
    beman::big_int::big_int x(-1);
    EXPECT_EQ(x.representation()[0], 1U);
}

TEST(BasicConstruction, IntegralConstructionWithAllocator) {
    std::allocator<beman::big_int::uint_multiprecision_t> a;
    beman::big_int::big_int                               x(42, a);
    EXPECT_EQ(x.width_mag(), 5U);
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(BasicConstruction, FromRangeConstruction) {
    std::array<beman::big_int::uint_multiprecision_t, 2> limbs{0xDEADBEEFU, 0xCAFEBABEU};
    beman::big_int::big_int                              x(std::from_range, limbs);
    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0xDEADBEEFU);
    EXPECT_EQ(x.representation()[1], 0xCAFEBABEU);
}

TEST(BasicConstruction, FromRangeConstructionTrimsLeadingZeros) {
    std::array<beman::big_int::uint_multiprecision_t, 3> limbs{1U, 0U, 0U};
    beman::big_int::big_int                              x(std::from_range, limbs);
    EXPECT_EQ(x.representation().size(), 1U);
}

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
