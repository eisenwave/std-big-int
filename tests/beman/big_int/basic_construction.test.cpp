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
