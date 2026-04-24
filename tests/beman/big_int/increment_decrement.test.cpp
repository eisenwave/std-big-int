// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>
#include <limits>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

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
    EXPECT_EQ(x, 42);
    EXPECT_EQ(y, -4);
    EXPECT_EQ(z, big_int{1} << 64);
}

TEST(IncrementDecrement, PostfixIncrement) {
    big_int x{41};
    big_int y{-5};

    const big_int old_x = x++;
    const big_int old_y = y++;

    EXPECT_EQ(old_x, 41);
    EXPECT_EQ(x, 42);
    EXPECT_EQ(old_y, -5);
    EXPECT_EQ(y, -4);
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
    EXPECT_EQ(p, 4);
    EXPECT_EQ(q, -1);
    EXPECT_EQ(r, -6);
    EXPECT_EQ(s, -(big_int{1} << 64));
    EXPECT_EQ((s <=> 0), std::strong_ordering::less);
}

TEST(IncrementDecrement, PostfixDecrement) {
    big_int x{41};
    big_int y{0};
    big_int z{-5};

    const big_int old_x = x--;
    const big_int old_y = y--;
    const big_int old_z = z--;

    EXPECT_EQ(old_x, 41);
    EXPECT_EQ(x, 40);
    EXPECT_EQ(old_y, 0);
    EXPECT_EQ(y, -1);
    EXPECT_EQ(old_z, -5);
    EXPECT_EQ(z, -6);
}

TEST(IncrementDecrement, PrefixIncrementRequiresAllocationForLargeValue) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    EXPECT_EQ(x.representation().size(), 1U);
    EXPECT_EQ(x.capacity(), 0U);

    ++x;

    EXPECT_EQ(x, big_int{1} << 64);
    EXPECT_GT(x.capacity(), 0U);
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

    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(c, 1);
    EXPECT_EQ(d, -1);
}

TEST(IncrementDecrement, PrefixIncrementAllocatedCarryChain) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    ++x;
    x = -x;

    EXPECT_GT(x.capacity(), 0U);
    EXPECT_EQ(x, -(big_int{1} << 64));

    ++x;

    EXPECT_EQ(x, -big_int{std::numeric_limits<uint_multiprecision_t>::max()});
    EXPECT_GT(x.capacity(), 0U);
}

TEST(IncrementDecrement, PrefixDecrementAllocatedBorrowChain) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    ++x;

    EXPECT_GT(x.capacity(), 0U);
    EXPECT_EQ(x, big_int{1} << 64);

    --x;

    EXPECT_EQ(x, big_int{std::numeric_limits<uint_multiprecision_t>::max()});
    EXPECT_GT(x.capacity(), 0U);
}

TEST(IncrementDecrement, BitwiseNotSmallIntegers) {
    big_int a{0};
    big_int b{1};
    big_int c{-1};
    big_int d{42};
    big_int e{-42};

    const big_int ra = ~a;
    const big_int rb = ~b;
    const big_int rc = ~c;
    const big_int rd = ~d;
    const big_int re = ~e;

    EXPECT_EQ(ra, -1);
    EXPECT_EQ(rb, -2);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(rd, -43);
    EXPECT_EQ(re, 41);

    const big_int rr = ~big_int{7};
    EXPECT_EQ(rr, -8);
}

TEST(IncrementDecrement, BitwiseNotBigInteger) {
    big_int x{1};
    x <<= 130;

    const big_int y = ~x;

    EXPECT_EQ((y <=> 0), std::strong_ordering::less);
    EXPECT_EQ(y, -x - 1);

    big_int       n = -x;
    const big_int z = ~n;

    EXPECT_EQ((z <=> 0), std::strong_ordering::greater);
    EXPECT_EQ(z, x - 1);
}

TEST(IncrementDecrement, BitwiseNotCanAllocate) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    EXPECT_EQ(x.capacity(), 0U);

    const big_int y = ~x;

    EXPECT_EQ(y, -x - 1);
    EXPECT_EQ((y <=> 0), std::strong_ordering::less);
    EXPECT_GT(y.capacity(), 0U);
}

} // namespace
