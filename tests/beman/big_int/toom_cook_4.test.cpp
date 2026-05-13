// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include "testing.hpp"
#include <gtest/gtest.h>
#include <cstddef>

namespace bmp = ::beman::big_int::boost_mp_testing;

constexpr std::size_t limb_bits =
    static_cast<std::size_t>(std::numeric_limits<::beman::big_int::uint_multiprecision_t>::digits);

// Toom-Cook 4 cutoff is 4500 limbs. Tests around the boundary verify both
// that the dispatcher correctly enters Toom-4 above the cutoff and that
// the algorithm produces correct results for varying input sizes.

void check_balanced(const std::size_t limbs_a, const std::size_t limbs_b) {
    const std::string a = bmp::random_big_int(limbs_a * limb_bits);
    const std::string b = bmp::random_big_int(limbs_b * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
}

TEST(Multiplication, ToomCook4AtCutoff) {
    // Both operands exactly at the Toom-Cook 4 cutoff. Forces dispatch into
    // Toom-4 with shallow recursion (k = 1125, so all sub-products fall back
    // to Toom-3 / Karatsuba).
    check_balanced(4500, 4500);
}

TEST(Multiplication, ToomCook4JustAboveCutoff) {
    check_balanced(4501, 4501);
    check_balanced(4600, 4600);
}

TEST(Multiplication, ToomCook4JustBelowCutoff) {
    // Both 4499 limbs: dispatcher uses Toom-3, not Toom-4. Sanity check
    // that the cutoff gate works as expected.
    check_balanced(4499, 4499);
}

TEST(Multiplication, ToomCook4DeepRecursion) {
    // Large enough to drive an extra Toom-4 recursion level.
    // At cutoff 4500, a 20000-limb call splits into k=5000 sub-products,
    // which clear the cutoff, so two Toom-4 levels run before the cascade
    // drops into Toom-3.
    check_balanced(20000, 20000);
}

TEST(Multiplication, ToomCook4AsymmetricBalanced) {
    // Asymmetric but within the algorithm's min > 3*k gate, so Toom-4 is
    // still used. For each pair, k = ceil(max/4) and we ensure min > 3*k.
    check_balanced(4700, 5500);  // k=1375, 3k=4125 < 4700
    check_balanced(5500, 6500);  // k=1625, 3k=4875 < 5500
    check_balanced(8000, 10000); // k=2500, 3k=7500 < 8000
}

TEST(Multiplication, ToomCook4AsymmetricFallback) {
    // Asymmetric beyond the gate: Toom-4 falls back to Toom-3 even though
    // both operands clear the cutoff. min <= 3*k violates the invariant that
    // both a3 and b3 are non-empty.
    check_balanced(4500, 8000);  // k=2000, 3k=6000 >= 4500 -> fallback
    check_balanced(5000, 12000); // k=3000, 3k=9000 >= 5000 -> fallback
}

TEST(Multiplication, ToomCook4Squaring) {
    // a * a (same operand) at sizes that exercise Toom-4.
    const std::string a = bmp::random_big_int(4700 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, a));

    const std::string b = bmp::random_big_int(6000 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, b, b));
}

TEST(Multiplication, ToomCook4SignedOperands) {
    // Negative operand handling is independent of the algorithm choice, but
    // confirm the sign propagation works at Toom-4 sizes.
    const std::string a = bmp::random_big_int(4700 * limb_bits, /*negative=*/true);
    const std::string b = bmp::random_big_int(4700 * limb_bits, /*negative=*/false);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));

    const std::string c = bmp::random_big_int(4700 * limb_bits, /*negative=*/true);
    const std::string d = bmp::random_big_int(4700 * limb_bits, /*negative=*/true);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, c, d));
}
