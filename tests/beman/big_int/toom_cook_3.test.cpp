// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include "testing.hpp"
#include <gtest/gtest.h>
#include <cstddef>

namespace bmp = ::beman::big_int::boost_mp_testing;

constexpr std::size_t limb_bits =
    static_cast<std::size_t>(std::numeric_limits<::beman::big_int::uint_multiprecision_t>::digits);

// Toom-Cook 3 cutoff is 120 limbs. Tests around the boundary verify both
// that the dispatcher correctly enters Toom-3 above the cutoff and that
// the algorithm produces correct results for varying input sizes.

void check_balanced(const std::size_t limbs_a, const std::size_t limbs_b) {
    const std::string a = bmp::random_big_int(limbs_a * limb_bits);
    const std::string b = bmp::random_big_int(limbs_b * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
}

TEST(Multiplication, ToomCook3AtCutoff) {
    // Both operands exactly at the Toom-Cook 3 cutoff. Forces dispatch into
    // Toom-3 with shallow recursion (k = 40, so all sub-products fall back
    // to Karatsuba).
    check_balanced(120, 120);
}

TEST(Multiplication, ToomCook3JustAboveCutoff) {
    check_balanced(121, 121);
    check_balanced(125, 125);
}

TEST(Multiplication, ToomCook3JustBelowCutoff) {
    // Both 119 limbs: dispatcher uses Karatsuba, not Toom-3. Sanity check
    // that the cutoff gate works as expected.
    check_balanced(119, 119);
}

TEST(Multiplication, ToomCook3DeepRecursion) {
    // Large enough to drive multiple Toom-3 recursion levels.
    check_balanced(400, 400);
    check_balanced(1100, 1100);
}

TEST(Multiplication, ToomCook3AsymmetricBalanced) {
    // Asymmetric but within the 3*min > 2*max gate, so Toom-3 is still used.
    // For min = 121, the gate requires 3*121 > 2*max, i.e., max < 181.5.
    check_balanced(121, 180);
    check_balanced(150, 200);
    check_balanced(200, 250);
}

TEST(Multiplication, ToomCook3AsymmetricFallback) {
    // Asymmetric beyond the gate: Toom-3 falls back to Karatsuba.
    // These exercise the fallback path with both operands above the
    // Toom-3 cutoff but with min <= 2*k.
    check_balanced(120, 500);
    check_balanced(150, 600);
}

TEST(Multiplication, ToomCook3Squaring) {
    // a * a (same operand) at sizes that exercise Toom-3.
    const std::string a = bmp::random_big_int(150 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, a));

    const std::string b = bmp::random_big_int(500 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, b, b));
}

TEST(Multiplication, ToomCook3SignedOperands) {
    // Negative operand handling is independent of the algorithm choice, but
    // confirm the sign propagation works at Toom-3 sizes.
    const std::string a = bmp::random_big_int(150 * limb_bits, /*negative=*/true);
    const std::string b = bmp::random_big_int(150 * limb_bits, /*negative=*/false);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));

    const std::string c = bmp::random_big_int(150 * limb_bits, /*negative=*/true);
    const std::string d = bmp::random_big_int(150 * limb_bits, /*negative=*/true);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, c, d));
}
