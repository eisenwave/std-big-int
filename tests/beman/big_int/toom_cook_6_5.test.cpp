// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include "testing.hpp"
#include <gtest/gtest.h>
#include <cstddef>

namespace bmp = ::beman::big_int::boost_mp_testing;

constexpr std::size_t limb_bits =
    static_cast<std::size_t>(std::numeric_limits<::beman::big_int::uint_multiprecision_t>::digits);

// Toom-Cook 6.5 cutoff is 3000 limbs. Tests around the boundary verify both
// that the dispatcher correctly enters Toom-6.5 above the cutoff and that
// the algorithm produces correct results for varying input sizes.

void check_balanced(const std::size_t limbs_a, const std::size_t limbs_b) {
    const std::string a = bmp::random_big_int(limbs_a * limb_bits);
    const std::string b = bmp::random_big_int(limbs_b * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
}

TEST(Multiplication, ToomCook6_5AtCutoff) {
    // Both operands exactly at the Toom-Cook 6.5 cutoff. Forces dispatch into
    // Toom-6.5 with shallow recursion (k = 500, so all sub-products fall back
    // to Toom-4 / Toom-3 / Karatsuba).
    check_balanced(3000, 3000);
}

TEST(Multiplication, ToomCook6_5JustAboveCutoff) {
    check_balanced(3001, 3001);
    check_balanced(3100, 3100);
}

TEST(Multiplication, ToomCook6_5JustBelowCutoff) {
    // Both 2999 limbs: dispatcher uses Toom-4, not Toom-6.5. Sanity check
    // that the cutoff gate works as expected.
    check_balanced(2999, 2999);
}

TEST(Multiplication, ToomCook6_5DeepRecursion) {
    // Large enough to drive an extra Toom-6.5 recursion level. At cutoff 3000,
    // a 20000-limb call splits into k ~= 3334 sub-products, which clear the
    // cutoff so two Toom-6.5 levels run before the cascade drops into Toom-4.
    check_balanced(20000, 20000);
}

TEST(Multiplication, ToomCook6_5AsymmetricBalanced) {
    // Asymmetric within the algorithm's min > 5*k and max <= 7*k gates, so
    // Toom-6.5 is still used. For each pair, k = ceil(min/6) and we ensure
    // min > 5*k and max <= 7*k.
    check_balanced(3000, 3300); // k=500, 5k=2500 < 3000, 7k=3500 >= 3300
    check_balanced(4200, 4800); // k=700, 5k=3500 < 4200, 7k=4900 >= 4800
    check_balanced(6000, 7000); // k=1000, 5k=5000 < 6000, 7k=7000 == 7000
}

TEST(Multiplication, ToomCook6_5AsymmetricFallback) {
    // Asymmetric beyond the 7:6 ratio gate: Toom-6.5 falls back to Toom-4
    // even though both operands clear the cutoff. max > 7*k violates the
    // invariant that b fits in seven pieces.
    check_balanced(3000, 4000); // k=500, 7k=3500 < 4000 -> fallback
    check_balanced(3500, 8000); // k=584, 7k=4088 < 8000 -> fallback
}

TEST(Multiplication, ToomCook6_5Squaring) {
    // a * a (same operand) at sizes that exercise Toom-6.5.
    const std::string a = bmp::random_big_int(3500 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, a));

    const std::string b = bmp::random_big_int(6000 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, b, b));
}

TEST(Multiplication, ToomCook6_5SignedOperands) {
    // Negative operand handling is independent of the algorithm choice, but
    // confirm the sign propagation works at Toom-6.5 sizes.
    const std::string a = bmp::random_big_int(3500 * limb_bits, /*negative=*/true);
    const std::string b = bmp::random_big_int(3500 * limb_bits, /*negative=*/false);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));

    const std::string c = bmp::random_big_int(3500 * limb_bits, /*negative=*/true);
    const std::string d = bmp::random_big_int(3500 * limb_bits, /*negative=*/true);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, c, d));
}
