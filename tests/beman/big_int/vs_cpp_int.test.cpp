// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include "boost_mp_testing.hpp"

#include <cstddef>
#include <functional>

namespace {

namespace bmp = ::beman::big_int::boost_mp_testing;
using bmp::check_cpp_int_equal;
using bmp::random_big_int;

// ----- specific-value tests -----
//
// Good for pinning down regressions at known-interesting inputs
// (all-ones magnitudes, single bits high up, values that straddle a limb
// boundary, etc.) without relying on the random sweep to happen to hit them.

TEST(VsCppInt, AddSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "1", "1"));
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "ffffffffffffffff", "1"));                  // limb carry out
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "ffffffffffffffffffffffffffffffff", "1"));  // two-limb carry out
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "-deadbeef", "deadbeef"));                  // cancel to zero
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{},
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef",
                                    "1234567890abcdeffedcba9876543210"));
}

TEST(VsCppInt, SubSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "1", "1"));
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "10000000000000000", "1"));  // borrow through limb
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "1", "10000000000000000"));  // negative result
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{}, "-ffffffff", "-ffffffff")); // negative cancel
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{},
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef",
                                    "1234567890abcdeffedcba9876543210"));
}

TEST(VsCppInt, MulSpecific) {
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "0", "deadbeef"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "ffffffff", "ffffffff"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "ffffffffffffffff", "ffffffffffffffff"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{}, "-abcdef", "123456"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{},
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef",
                                    "1234567890abcdeffedcba9876543210"));
}

// ----- random parity sweep -----
//
// Bit widths chosen to exercise a spread of limb counts:
//   32 bits -> single small limb
//  160 bits -> a few limbs
//  800 bits -> ~13 limbs, result of mul spans ~26 limbs
// 2000 bits -> large multi-limb, clearly heap-allocated

constexpr std::size_t kBitWidths[]   = {32, 160, 800, 2000};
constexpr std::size_t kTrialsPerSize = 25;

template <class BinOp>
void run_parity_sweep(BinOp op) {
    for (const std::size_t lhs_bits : kBitWidths) {
        for (const std::size_t rhs_bits : kBitWidths) {
            for (std::size_t trial = 0; trial < kTrialsPerSize; ++trial) {
                const bool lhs_neg = (trial & 0b01) != 0;
                const bool rhs_neg = (trial & 0b10) != 0;
                ASSERT_TRUE(
                    check_cpp_int_equal(op, random_big_int(lhs_bits, lhs_neg), random_big_int(rhs_bits, rhs_neg)))
                    << "lhs_bits=" << lhs_bits << " rhs_bits=" << rhs_bits << " trial=" << trial;
            }
        }
    }
}

TEST(VsCppInt, AddRandom) { run_parity_sweep(std::plus<>{}); }
TEST(VsCppInt, SubRandom) { run_parity_sweep(std::minus<>{}); }
TEST(VsCppInt, MulRandom) { run_parity_sweep(std::multiplies<>{}); }

// Exercises zero-operand corner cases, which the random sweep is unlikely to
// hit on its own.
TEST(VsCppInt, ZeroCornerCases) {
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{}, "0", "0"));
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{},
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef",
                                    "0"));
    EXPECT_TRUE(check_cpp_int_equal(std::plus<>{},
                                    "0",
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef"));
    EXPECT_TRUE(check_cpp_int_equal(std::minus<>{},
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef",
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef"));
    EXPECT_TRUE(check_cpp_int_equal(std::multiplies<>{},
                                    "deadbeefcafebabef00dfacec0ffee1234567890abcdef",
                                    "0"));
}

} // namespace
