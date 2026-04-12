// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <compare>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

namespace {

using beman::big_int::big_int;
using beman::big_int::detail::int_multiprecision_t;

TEST(Comparison, EqualitySmallIntegers) {
    const big_int zero{0};
    const big_int one{1};
    const big_int neg_one{-1};

    EXPECT_EQ(zero == zero, true);
    EXPECT_EQ(zero == 0U, true);
    EXPECT_EQ(0U == zero, true);
    EXPECT_EQ(zero == 0, true);
    EXPECT_EQ(0 == zero, true);

    EXPECT_EQ(zero == one, false);
    EXPECT_EQ(zero == 1U, false);
    EXPECT_EQ(0U == one, false);
    EXPECT_EQ(zero == 1, false);
    EXPECT_EQ(0 == one, false);

    EXPECT_EQ(one == zero, false);
    EXPECT_EQ(one == 0U, false);
    EXPECT_EQ(1U == zero, false);
    EXPECT_EQ(one == 0, false);
    EXPECT_EQ(1 == zero, false);

    EXPECT_EQ(one == one, true);
    EXPECT_EQ(one == 1U, true);
    EXPECT_EQ(1U == one, true);
    EXPECT_EQ(one == 1, true);
    EXPECT_EQ(1 == one, true);

    EXPECT_EQ(one == neg_one, false);
    EXPECT_EQ(one == -1, false);
    EXPECT_EQ(1U == neg_one, false);
    EXPECT_EQ(1 == neg_one, false);

    EXPECT_EQ(neg_one == one, false);
    EXPECT_EQ(neg_one == 1U, false);
    EXPECT_EQ(neg_one == 1, false);
    EXPECT_EQ((-1) == one, false);

    EXPECT_EQ(neg_one == neg_one, true);
    EXPECT_EQ(neg_one == -1, true);
    EXPECT_EQ((-1) == neg_one, true);
}

TEST(Comparison, EqualityWideIntegers) {
    const big_int wide_one{1.0e20};
    const big_int wide_two{2.0e20};
    const big_int wide_neg = -wide_one;

    EXPECT_EQ(wide_one == wide_one, true);
    EXPECT_EQ(wide_two == wide_two, true);
    EXPECT_EQ(wide_one == wide_two, false);
    EXPECT_EQ(wide_two == wide_one, false);
    EXPECT_EQ(wide_one == 1U, false);
    EXPECT_EQ(wide_one == big_int{1U}, false);
    EXPECT_EQ(1U == wide_one, false);
    EXPECT_EQ(big_int{1U} == wide_one, false);
    EXPECT_EQ(1 == wide_one, false);
    EXPECT_EQ(big_int{1} == wide_one, false);
    EXPECT_EQ(wide_one == wide_neg, false);
    EXPECT_EQ(wide_neg == wide_one, false);
    EXPECT_EQ(wide_neg == -wide_one, true);
    EXPECT_EQ((-wide_one) == wide_neg, true);
}

TEST(Comparison, OrderingSmallIntegers) {
    constexpr big_int zero{0};
    constexpr big_int one{1};
    constexpr big_int two{2};
    constexpr big_int neg_one{-1};

    EXPECT_EQ((zero <=> zero), std::strong_ordering::equal);
    EXPECT_EQ((zero <=> 0u), std::strong_ordering::equal);
    EXPECT_EQ((0u <=> zero), std::strong_ordering::equal);
    EXPECT_EQ((zero <=> 0), std::strong_ordering::equal);
    EXPECT_EQ((0 <=> zero), std::strong_ordering::equal);

    EXPECT_EQ((zero <=> one), std::strong_ordering::less);
    EXPECT_EQ((zero <=> 1u), std::strong_ordering::less);
    EXPECT_EQ((0u <=> one), std::strong_ordering::less);
    EXPECT_EQ((zero <=> 1), std::strong_ordering::less);
    EXPECT_EQ((0 <=> one), std::strong_ordering::less);

    EXPECT_EQ((one <=> zero), std::strong_ordering::greater);
    EXPECT_EQ((one <=> 0u), std::strong_ordering::greater);
    EXPECT_EQ((1u <=> zero), std::strong_ordering::greater);
    EXPECT_EQ((one <=> 0), std::strong_ordering::greater);
    EXPECT_EQ((1 <=> zero), std::strong_ordering::greater);

    EXPECT_EQ((one <=> one), std::strong_ordering::equal);
    EXPECT_EQ((one <=> 1u), std::strong_ordering::equal);
    EXPECT_EQ((1U <=> one), std::strong_ordering::equal);
    EXPECT_EQ((one <=> 1), std::strong_ordering::equal);
    EXPECT_EQ((1 <=> one), std::strong_ordering::equal);

    EXPECT_EQ((one <=> two), std::strong_ordering::less);
    EXPECT_EQ((one <=> 2u), std::strong_ordering::less);
    EXPECT_EQ((1u <=> two), std::strong_ordering::less);
    EXPECT_EQ((one <=> 2), std::strong_ordering::less);
    EXPECT_EQ((1 <=> two), std::strong_ordering::less);

    EXPECT_EQ((two <=> one), std::strong_ordering::greater);
    EXPECT_EQ((two <=> 1u), std::strong_ordering::greater);
    EXPECT_EQ((2u <=> one), std::strong_ordering::greater);
    EXPECT_EQ((two <=> 1), std::strong_ordering::greater);
    EXPECT_EQ((2 <=> one), std::strong_ordering::greater);

    EXPECT_EQ((one <=> neg_one), std::strong_ordering::greater);
    EXPECT_EQ((one <=> -1), std::strong_ordering::greater);
    EXPECT_EQ((1u <=> neg_one), std::strong_ordering::greater);
    EXPECT_EQ((1 <=> neg_one), std::strong_ordering::greater);

    EXPECT_EQ((neg_one <=> one), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 1u), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 1), std::strong_ordering::less);
    EXPECT_EQ((-1 <=> one), std::strong_ordering::less);

    EXPECT_EQ((neg_one <=> two), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 2u), std::strong_ordering::less);
    EXPECT_EQ((neg_one <=> 2), std::strong_ordering::less);
    EXPECT_EQ((-1 <=> two), std::strong_ordering::less);

    EXPECT_EQ((neg_one <=> -1), std::strong_ordering::equal);
    EXPECT_EQ((-1 <=> neg_one), std::strong_ordering::equal);
}

TEST(Comparison, OrderingWideIntegers) {
    const big_int wide_one{1.0e20};
    const big_int wide_two{2.0e20};
    const big_int wide_neg = -wide_one;

    EXPECT_EQ((wide_one <=> wide_one), std::strong_ordering::equal);
    EXPECT_EQ((wide_two <=> wide_two), std::strong_ordering::equal);
    EXPECT_EQ((wide_one <=> wide_two), std::strong_ordering::less);
    EXPECT_EQ((wide_two <=> wide_one), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> 1U), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> big_int{1U}), std::strong_ordering::greater);
    EXPECT_EQ((1U <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((big_int{1U} <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((wide_one <=> 1), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> big_int{1}), std::strong_ordering::greater);
    EXPECT_EQ((1 <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((big_int{1} <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((wide_one <=> -1), std::strong_ordering::greater);
    EXPECT_EQ((wide_one <=> big_int{-1}), std::strong_ordering::greater);
    EXPECT_EQ((-1 <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((big_int{-1} <=> wide_one), std::strong_ordering::less);
    EXPECT_EQ((wide_neg <=> 1), std::strong_ordering::less);
    EXPECT_EQ((wide_neg <=> big_int{1}), std::strong_ordering::less);
    EXPECT_EQ((1 <=> wide_neg), std::strong_ordering::greater);
    EXPECT_EQ((big_int{1} <=> wide_neg), std::strong_ordering::greater);
}

} // namespace
