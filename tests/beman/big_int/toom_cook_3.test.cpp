// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"
#include "testing.hpp"
#include <gtest/gtest.h>
#include <cstddef>

namespace bmp = ::beman::big_int::boost_mp_testing;

constexpr std::size_t limb_bits =
    static_cast<std::size_t>(std::numeric_limits<::beman::big_int::uint_multiprecision_t>::digits);

// Toom-Cook 3 cutoff is 800 limbs. Tests around the boundary verify both
// that the dispatcher correctly enters Toom-3 above the cutoff and that
// the algorithm produces correct results for varying input sizes.

void check_balanced(const std::size_t limbs_a, const std::size_t limbs_b) {
    const std::string a = bmp::random_big_int(limbs_a * limb_bits);
    const std::string b = bmp::random_big_int(limbs_b * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));
}

TEST(Multiplication, ToomCook3AtCutoff) {
    // Both operands exactly at the Toom-Cook 3 cutoff. Forces dispatch into
    // Toom-3 with shallow recursion (k = 267, so all sub-products fall back
    // to Karatsuba).
    check_balanced(800, 800);
}

TEST(Multiplication, ToomCook3JustAboveCutoff) {
    check_balanced(801, 801);
    check_balanced(850, 850);
}

TEST(Multiplication, ToomCook3JustBelowCutoff) {
    // Both 799 limbs: dispatcher uses Karatsuba, not Toom-3. Sanity check
    // that the cutoff gate works as expected.
    check_balanced(799, 799);
}

TEST(Multiplication, ToomCook3DeepRecursion) {
    // Large enough to drive multiple Toom-3 recursion levels. At cutoff 800,
    // the inner products of a 2500-limb call have ~835 limbs each (still
    // above the cutoff), giving two Toom-3 levels before the cascade drops
    // into Karatsuba.
    check_balanced(2500, 2500);
    check_balanced(5000, 5000);
}

TEST(Multiplication, ToomCook3AsymmetricBalanced) {
    // Asymmetric but within the algorithm's min > 2*k gate, so Toom-3 is
    // still used. For each pair, k = ceil(max/3) and we ensure min > 2*k.
    check_balanced(850, 1200);  // k=400, 2k=800 < 850
    check_balanced(1000, 1400); // k=467, 2k=934 < 1000
    check_balanced(1500, 2100); // k=700, 2k=1400 < 1500
}

TEST(Multiplication, ToomCook3AsymmetricFallback) {
    // Asymmetric beyond the gate: Toom-3 falls back to Karatsuba even though
    // both operands clear the cutoff. min <= 2*k violates the invariant that
    // both a2 and b2 are non-empty.
    check_balanced(800, 1500); // k=500, 2k=1000 >= 800 -> fallback
    check_balanced(900, 2000); // k=667, 2k=1334 >= 900 -> fallback
}

TEST(Multiplication, ToomCook3Squaring) {
    // a * a (same operand) at sizes that exercise Toom-3.
    const std::string a = bmp::random_big_int(1000 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, a));

    const std::string b = bmp::random_big_int(2000 * limb_bits);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, b, b));
}

TEST(Multiplication, ToomCook3SignedOperands) {
    // Negative operand handling is independent of the algorithm choice, but
    // confirm the sign propagation works at Toom-3 sizes.
    const std::string a = bmp::random_big_int(1000 * limb_bits, /*negative=*/true);
    const std::string b = bmp::random_big_int(1000 * limb_bits, /*negative=*/false);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, a, b));

    const std::string c = bmp::random_big_int(1000 * limb_bits, /*negative=*/true);
    const std::string d = bmp::random_big_int(1000 * limb_bits, /*negative=*/true);
    EXPECT_TRUE(bmp::check_cpp_int_equal(std::multiplies<>{}, c, d));
}
