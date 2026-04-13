// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <limits>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

namespace {

using beman::big_int::big_int;
using beman::big_int::uint_multiprecision_t;
using beman::big_int::detail::int_multiprecision_t;

TEST(BitShift, LeftShiftBasic) {
    big_int x{0};
    big_int y{1};
    big_int z{1};
    big_int w{-1};
    big_int q{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 0;
    y <<= 0;
    z <<= 1;
    w <<= 1;
    q <<= 1;

    EXPECT_EQ(x == 0, true);
    EXPECT_EQ(y == 1, true);
    EXPECT_EQ(z.representation().size(), 1U);
    EXPECT_EQ(z.representation()[0], 2ULL);
    EXPECT_EQ(z == 2, true);
    EXPECT_EQ(w == -2, true);
    EXPECT_EQ(q.representation().size(), 2U);
    EXPECT_EQ(q.representation()[0], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(q.representation()[1], 1ULL);
}

TEST(BitShift, LeftShiftAcrossLimbs) {
    big_int x{1};
    big_int y{1};
    big_int z{1};
    big_int expected{1};

    x <<= 64;
    y <<= 65;
    z <<= 128;
    expected <<= 64;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], 1ULL);
    EXPECT_EQ(x == expected, true);

    EXPECT_EQ(y.representation().size(), 2U);
    EXPECT_EQ(y.representation()[0], 0ULL);
    EXPECT_EQ(y.representation()[1], 2ULL);

    EXPECT_EQ(z.representation().size(), 3U);
    EXPECT_EQ(z.representation()[0], 0ULL);
    EXPECT_EQ(z.representation()[1], 0ULL);
    EXPECT_EQ(z.representation()[2], 1ULL);
}

TEST(BitShift, LeftShiftAllocatedValue) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 1;
    x <<= 64;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(x.capacity() > 0U, true);
}

TEST(BitShift, RightShiftBasic) {
    big_int x{0};
    big_int y{1};
    big_int z{2};
    big_int w{-1};
    big_int q{-2};

    x >>= 0;
    y >>= 0;
    z >>= 1;
    w >>= 1;
    q >>= 1;

    EXPECT_EQ(x == 0, true);
    EXPECT_EQ(y == 1, true);
    EXPECT_EQ(z == 1, true);
    EXPECT_EQ(w == -1, true);
    EXPECT_EQ(q == -1, true);
}

TEST(BitShift, RightShiftAcrossLimbs) {
    big_int x{1};
    big_int y{1};
    big_int z{1};

    x <<= 64;
    y <<= 65;
    z <<= 128;

    x >>= 64;
    y >>= 64;
    z >>= 128;

    EXPECT_EQ(x == 1, true);
    EXPECT_EQ(y == 2, true);
    EXPECT_EQ(z == 1, true);
}

TEST(BitShift, RightShiftNegativeRounding) {
    big_int x{-3};
    big_int y{-4};
    big_int z{-std::numeric_limits<uint_multiprecision_t>::max()};

    x >>= 1;
    y >>= 1;
    z >>= 64;

    EXPECT_EQ(x == -2, true);
    EXPECT_EQ(y == -2, true);
    EXPECT_EQ(z == -1, true);
}

} // namespace
