// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>
#include <limits>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

namespace {

using beman::big_int::big_int;
using beman::big_int::uint_multiprecision_t;
using beman::big_int::detail::int_multiprecision_t;

TEST(IncrementDecrement, PrefixIncrement) {
    big_int x{41};
    big_int y{-5};
    big_int z{std::numeric_limits<uint_multiprecision_t>::max()};

    big_int& rx = ++x;
    big_int& ry = ++y;
    big_int& rz = ++z;

    EXPECT_EQ(&rx, &x);
    EXPECT_EQ(&ry, &y);
    EXPECT_EQ(&rz, &z);
    EXPECT_EQ(x == 42, true);
    EXPECT_EQ(y == -4, true);
    EXPECT_EQ(z.representation().size(), 2U);
    EXPECT_EQ(z.representation()[0], 0ULL);
    EXPECT_EQ(z.representation()[1], 1ULL);
}

TEST(IncrementDecrement, PostfixIncrement) {
    big_int x{41};
    big_int y{-5};

    const big_int old_x = x++;
    const big_int old_y = y++;

    EXPECT_EQ(old_x == 41, true);
    EXPECT_EQ(x == 42, true);
    EXPECT_EQ(old_y == -5, true);
    EXPECT_EQ(y == -4, true);
}

TEST(IncrementDecrement, PrefixDecrement) {
    big_int p{5};
    big_int q{0};
    big_int r{-5};
    big_int s{std::numeric_limits<uint_multiprecision_t>::max()};
    s = -s;

    big_int& rp = --p;
    big_int& rq = --q;
    big_int& rr = --r;
    big_int& rs = --s;

    EXPECT_EQ(&rp, &p);
    EXPECT_EQ(&rq, &q);
    EXPECT_EQ(&rr, &r);
    EXPECT_EQ(&rs, &s);
    EXPECT_EQ(p == 4, true);
    EXPECT_EQ(q == -1, true);
    EXPECT_EQ(r == -6, true);
    ASSERT_EQ(s.representation().size(), 2U);
    EXPECT_EQ(s.representation()[0], 0ULL);
    EXPECT_EQ(s.representation()[1], 1ULL);
    EXPECT_EQ((s <=> 0), std::strong_ordering::less);
}

TEST(IncrementDecrement, PostfixDecrement) {
    big_int x{41};
    big_int y{0};
    big_int z{-5};

    const big_int old_x = x--;
    const big_int old_y = y--;
    const big_int old_z = z--;

    EXPECT_EQ(old_x == 41, true);
    EXPECT_EQ(x == 40, true);
    EXPECT_EQ(old_y == 0, true);
    EXPECT_EQ(y == -1, true);
    EXPECT_EQ(old_z == -5, true);
    EXPECT_EQ(z == -6, true);
}

TEST(IncrementDecrement, PrefixIncrementRequiresAllocationForLargeValue) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    EXPECT_EQ(x.representation().size(), 1U);
    EXPECT_EQ(x.capacity(), 0U);

    ++x;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], 1ULL);
    EXPECT_EQ(x.capacity() > 0U, true);
}

TEST(IncrementDecrement, ZeroAndOneTransitions) {
    big_int a{1};
    big_int b{-1};
    big_int c{0};
    big_int d{0};

    --a;
    ++b;
    ++c;
    --d;

    EXPECT_EQ(a == 0, true);
    EXPECT_EQ(b == 0, true);
    EXPECT_EQ(c == 1, true);
    EXPECT_EQ(d == -1, true);
}

TEST(IncrementDecrement, PrefixIncrementAllocatedCarryChain) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    ++x;
    x = -x;

    EXPECT_EQ(x.capacity() > 0U, true);
    ASSERT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], 1ULL);

    ++x;

    EXPECT_EQ(x == -big_int{std::numeric_limits<uint_multiprecision_t>::max()}, true);
    ASSERT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(x.representation()[1], 0ULL);
    EXPECT_EQ(x.capacity() > 0U, true);
}

TEST(IncrementDecrement, PrefixDecrementAllocatedBorrowChain) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    ++x;

    EXPECT_EQ(x.capacity() > 0U, true);
    ASSERT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], 1ULL);

    --x;

    EXPECT_EQ(x == big_int{std::numeric_limits<uint_multiprecision_t>::max()}, true);
    ASSERT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(x.representation()[1], 0ULL);
    EXPECT_EQ(x.capacity() > 0U, true);
}

} // namespace
