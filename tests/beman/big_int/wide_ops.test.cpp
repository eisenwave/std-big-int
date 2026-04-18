// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/wide_ops.hpp>
#include <gtest/gtest.h>

#include <limits>

namespace {

using beman::big_int::uint_multiprecision_t;
using beman::big_int::detail::int_multiprecision_t;

TEST(WideOps, WideFromToIntUint) {
    using beman::big_int::detail::wide;
    using beman::big_int::detail::wider_t;

    constexpr wider_t<uint_multiprecision_t> x1 = (static_cast<wider_t<uint_multiprecision_t>>(1ull) << 64) | 9ull;
    constexpr wider_t<uint_multiprecision_t> x2 =
        (static_cast<wider_t<uint_multiprecision_t>>(0xA5ull) << 64) | 0x5Aull;
    constexpr wider_t<uint_multiprecision_t> x3 = 0;
    constexpr wider_t<uint_multiprecision_t> x4 =
        static_cast<wider_t<uint_multiprecision_t>>(std::numeric_limits<uint_multiprecision_t>::max());

    constexpr auto w1 = wide<uint_multiprecision_t>::from_int(x1);
    constexpr auto w2 = wide<uint_multiprecision_t>::from_int(x2);
    constexpr auto w3 = wide<uint_multiprecision_t>::from_int(x3);
    constexpr auto w4 = wide<uint_multiprecision_t>::from_int(x4);

    EXPECT_EQ(w1.low_bits, 9ull);
    EXPECT_EQ(w1.high_bits, 1ull);
    EXPECT_EQ(w1.to_int(), x1);
    EXPECT_EQ(w2.low_bits, 0x5Aull);
    EXPECT_EQ(w2.high_bits, 0xA5ull);
    EXPECT_EQ(w2.to_int(), x2);
    EXPECT_EQ(w3.low_bits, 0ull);
    EXPECT_EQ(w3.high_bits, 0ull);
    EXPECT_EQ(w3.to_int(), x3);
    EXPECT_EQ(w4.low_bits, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(w4.high_bits, 0ull);
    EXPECT_EQ(w4.to_int(), x4);
}

TEST(WideOps, WideFromToIntInt) {
    using beman::big_int::detail::wide;
    using beman::big_int::detail::wider_t;

    constexpr wider_t<int_multiprecision_t> x1 = static_cast<wider_t<int_multiprecision_t>>(-1);
    constexpr wider_t<int_multiprecision_t> x2 = static_cast<wider_t<int_multiprecision_t>>(1);
    constexpr wider_t<int_multiprecision_t> x3 = (static_cast<wider_t<int_multiprecision_t>>(-2) << 64) | 7;
    constexpr wider_t<int_multiprecision_t> x4 = 0;

    constexpr auto w1 = wide<int_multiprecision_t>::from_int(x1);
    constexpr auto w2 = wide<int_multiprecision_t>::from_int(x2);
    constexpr auto w3 = wide<int_multiprecision_t>::from_int(x3);
    constexpr auto w4 = wide<int_multiprecision_t>::from_int(x4);

    EXPECT_EQ(w1.low_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(w1.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(w1.to_int(), x1);
    EXPECT_EQ(w2.low_bits, static_cast<int_multiprecision_t>(1));
    EXPECT_EQ(w2.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(w2.to_int(), x2);
    EXPECT_EQ(w3.low_bits, static_cast<int_multiprecision_t>(7));
    EXPECT_EQ(w3.high_bits, static_cast<int_multiprecision_t>(-2));
    EXPECT_EQ(w3.to_int(), x3);
    EXPECT_EQ(w4.low_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(w4.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(w4.to_int(), x4);
}

TEST(WideOps, WideEquality) {
    using beman::big_int::detail::wide;

    constexpr wide<uint_multiprecision_t> a{.low_bits = 1ull, .high_bits = 2ull};
    constexpr wide<uint_multiprecision_t> b{.low_bits = 1ull, .high_bits = 2ull};
    constexpr wide<uint_multiprecision_t> c{.low_bits = 2ull, .high_bits = 2ull};
    constexpr wide<uint_multiprecision_t> d{.low_bits = 1ull, .high_bits = 3ull};

    EXPECT_EQ(a == b, true);
    EXPECT_EQ(b == a, true);
    EXPECT_EQ(a == c, false);
    EXPECT_EQ(c == a, false);
    EXPECT_EQ(a == d, false);
    EXPECT_EQ(d == a, false);
    EXPECT_EQ(b == c, false);
    EXPECT_EQ(c == b, false);
    EXPECT_EQ(b == d, false);
    EXPECT_EQ(d == b, false);
    EXPECT_EQ(c == d, false);
    EXPECT_EQ(d == c, false);
}

TEST(WideOps, WideningMulInt) {
    using beman::big_int::detail::widening_mul;

    const auto r1 = widening_mul<int_multiprecision_t>(0, 0);
    const auto r2 = widening_mul<int_multiprecision_t>(1, 1);
    const auto r3 = widening_mul<int_multiprecision_t>(-1, 1);
    const auto r4 = widening_mul<int_multiprecision_t>(-3, 5);
    const auto r5 = widening_mul<int_multiprecision_t>(7, -8);
    const auto r6 = widening_mul<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), 2);

    EXPECT_EQ(r1.low_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r1.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r2.low_bits, static_cast<int_multiprecision_t>(1));
    EXPECT_EQ(r2.high_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r3.low_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r3.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r4.low_bits, static_cast<int_multiprecision_t>(-15));
    EXPECT_EQ(r4.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r5.low_bits, static_cast<int_multiprecision_t>(-56));
    EXPECT_EQ(r5.high_bits, static_cast<int_multiprecision_t>(-1));
    EXPECT_EQ(r6.low_bits, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r6.high_bits, static_cast<int_multiprecision_t>(-1));
}

TEST(WideOps, WideningMulUint) {
    using beman::big_int::detail::widening_mul;

    const auto r1 = widening_mul<uint_multiprecision_t>(0ull, 0ull);
    const auto r2 = widening_mul<uint_multiprecision_t>(1ull, 1ull);
    const auto r3 = widening_mul<uint_multiprecision_t>(3ull, 7ull);
    const auto r4 = widening_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 2ull);
    const auto r5 = widening_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(),
                                                        std::numeric_limits<uint_multiprecision_t>::max());
    const auto r6 = widening_mul<uint_multiprecision_t>(1ull << 32, 1ull << 32);

    EXPECT_EQ(r1.low_bits, 0ull);
    EXPECT_EQ(r1.high_bits, 0ull);
    EXPECT_EQ(r2.low_bits, 1ull);
    EXPECT_EQ(r2.high_bits, 0ull);
    EXPECT_EQ(r3.low_bits, 21ull);
    EXPECT_EQ(r3.high_bits, 0ull);
    EXPECT_EQ(r4.low_bits, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_EQ(r4.high_bits, 1ull);
    EXPECT_EQ(r5.low_bits, 1ull);
    EXPECT_EQ(r5.high_bits, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_EQ(r6.low_bits, 0ull);
    EXPECT_EQ(r6.high_bits, 1ull);
}

TEST(WideOps, FunnelShlInt) {
    using beman::big_int::detail::funnel_shl;
    using beman::big_int::detail::wide;

    constexpr wide<int_multiprecision_t> x{
        .low_bits  = static_cast<int_multiprecision_t>(0x0102'0304'0506'0708ull),
        .high_bits = 0,
    };

    EXPECT_EQ(funnel_shl(x, 0), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 1), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 2), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 3), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 4), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 5), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 6), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 7), static_cast<int_multiprecision_t>(0x0000'0000'0000'0000ull));
    EXPECT_EQ(funnel_shl(x, 8), static_cast<int_multiprecision_t>(0x0000'0000'0000'0001ull));
    EXPECT_EQ(funnel_shl(x, 9), static_cast<int_multiprecision_t>(0x0000'0000'0000'0002ull));
    EXPECT_EQ(funnel_shl(x, 10), static_cast<int_multiprecision_t>(0x0000'0000'0000'0004ull));
    EXPECT_EQ(funnel_shl(x, 11), static_cast<int_multiprecision_t>(0x0000'0000'0000'0008ull));
}

TEST(WideOps, FunnelShlUint) {
    using beman::big_int::detail::funnel_shl;
    using beman::big_int::detail::wide;

    constexpr wide<uint_multiprecision_t> x{
        .low_bits  = 0x0123'4567'89AB'CDEFull,
        .high_bits = 0xFEDC'BA98'7654'3210ull,
    };

    EXPECT_EQ(funnel_shl(x, 0), 0xFEDC'BA98'7654'3210ull);
    EXPECT_EQ(funnel_shl(x, 1), 0xFDB9'7530'ECA8'6420ull);
    EXPECT_EQ(funnel_shl(x, 2), 0xFB72'EA61'D950'C840ull);
    EXPECT_EQ(funnel_shl(x, 3), 0xF6E5'D4C3'B2A1'9080ull);
    EXPECT_EQ(funnel_shl(x, 4), 0xEDCB'A987'6543'2100ull);
    EXPECT_EQ(funnel_shl(x, 5), 0xDB97'530E'CA86'4200ull);
    EXPECT_EQ(funnel_shl(x, 6), 0xB72E'A61D'950C'8400ull);
    EXPECT_EQ(funnel_shl(x, 7), 0x6E5D'4C3B'2A19'0800ull);
    EXPECT_EQ(funnel_shl(x, 8), 0xDCBA'9876'5432'1001ull);
    EXPECT_EQ(funnel_shl(x, 9), 0xB975'30EC'A864'2002ull);
    EXPECT_EQ(funnel_shl(x, 10), 0x72EA'61D9'50C8'4004ull);
    EXPECT_EQ(funnel_shl(x, 11), 0xE5D4'C3B2'A190'8009ull);
}

TEST(WideOps, FunnelShrInt) {
    using beman::big_int::detail::funnel_shr;
    using beman::big_int::detail::wide;

    constexpr wide<int_multiprecision_t> x{
        .low_bits  = static_cast<int_multiprecision_t>(0x0102'0304'0506'0708ull),
        .high_bits = 0,
    };

    EXPECT_EQ(funnel_shr(x, 0), static_cast<int_multiprecision_t>(0x0102'0304'0506'0708ull));
    EXPECT_EQ(funnel_shr(x, 1), static_cast<int_multiprecision_t>(0x0081'0182'0283'0384ull));
    EXPECT_EQ(funnel_shr(x, 2), static_cast<int_multiprecision_t>(0x0040'80C1'0141'81C2ull));
    EXPECT_EQ(funnel_shr(x, 3), static_cast<int_multiprecision_t>(0x0020'4060'80A0'C0E1ull));
    EXPECT_EQ(funnel_shr(x, 4), static_cast<int_multiprecision_t>(0x0010'2030'4050'6070ull));
    EXPECT_EQ(funnel_shr(x, 5), static_cast<int_multiprecision_t>(0x0008'1018'2028'3038ull));
    EXPECT_EQ(funnel_shr(x, 6), static_cast<int_multiprecision_t>(0x0004'080C'1014'181Cull));
    EXPECT_EQ(funnel_shr(x, 7), static_cast<int_multiprecision_t>(0x0002'0406'080A'0C0Eull));
    EXPECT_EQ(funnel_shr(x, 8), static_cast<int_multiprecision_t>(0x0001'0203'0405'0607ull));
    EXPECT_EQ(funnel_shr(x, 9), static_cast<int_multiprecision_t>(0x0000'8101'8202'8303ull));
    EXPECT_EQ(funnel_shr(x, 10), static_cast<int_multiprecision_t>(0x0000'4080'C101'4181ull));
    EXPECT_EQ(funnel_shr(x, 11), static_cast<int_multiprecision_t>(0x0000'2040'6080'A0C0ull));
}

TEST(WideOps, FunnelShrUint) {
    using beman::big_int::detail::funnel_shr;
    using beman::big_int::detail::wide;

    constexpr wide<uint_multiprecision_t> x{
        .low_bits  = 0x0011'2233'4455'6677ull,
        .high_bits = 0x8899'AABB'CCDD'EEFFull,
    };

    EXPECT_EQ(funnel_shr(x, 0), 0x0011'2233'4455'6677ull);
    EXPECT_EQ(funnel_shr(x, 1), 0x8008'9119'A22A'B33Bull);
    EXPECT_EQ(funnel_shr(x, 2), 0xC004'488C'D115'599Dull);
    EXPECT_EQ(funnel_shr(x, 3), 0xE002'2446'688A'ACCEull);
    EXPECT_EQ(funnel_shr(x, 4), 0xF001'1223'3445'5667ull);
    EXPECT_EQ(funnel_shr(x, 5), 0xF800'8911'9A22'AB33ull);
    EXPECT_EQ(funnel_shr(x, 6), 0xFC00'4488'CD11'5599ull);
    EXPECT_EQ(funnel_shr(x, 7), 0xFE00'2244'6688'AACCull);
    EXPECT_EQ(funnel_shr(x, 8), 0xFF00'1122'3344'5566ull);
    EXPECT_EQ(funnel_shr(x, 9), 0x7F80'0891'19A2'2AB3ull);
    EXPECT_EQ(funnel_shr(x, 10), 0xBFC0'0448'8CD1'1559ull);
    EXPECT_EQ(funnel_shr(x, 11), 0xDFE0'0224'4668'8AACull);
}

TEST(WideOps, OverflowingAddInt) {
    using beman::big_int::detail::overflowing_add;

    const auto r1 = overflowing_add<int_multiprecision_t>(0, 0);
    const auto r2 = overflowing_add<int_multiprecision_t>(7, 5);
    const auto r3 = overflowing_add<int_multiprecision_t>(-7, -5);
    const auto r4 = overflowing_add<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::max(), 1);
    const auto r5 = overflowing_add<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), -1);
    const auto r6 = overflowing_add<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::max(), -1);

    EXPECT_EQ(r1.value, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r1.overflow, false);
    EXPECT_EQ(r2.value, static_cast<int_multiprecision_t>(12));
    EXPECT_EQ(r2.overflow, false);
    EXPECT_EQ(r3.value, static_cast<int_multiprecision_t>(-12));
    EXPECT_EQ(r3.overflow, false);
    EXPECT_EQ(r4.value, std::numeric_limits<int_multiprecision_t>::min());
    EXPECT_EQ(r4.overflow, true);
    EXPECT_EQ(r5.value, std::numeric_limits<int_multiprecision_t>::max());
    EXPECT_EQ(r5.overflow, true);
    EXPECT_EQ(r6.value, std::numeric_limits<int_multiprecision_t>::max() - 1);
    EXPECT_EQ(r6.overflow, false);
}

TEST(WideOps, OverflowingAddUint) {
    using beman::big_int::detail::overflowing_add;

    const auto r1 = overflowing_add<uint_multiprecision_t>(0ull, 0ull);
    const auto r2 = overflowing_add<uint_multiprecision_t>(1ull, 2ull);
    const auto r3 = overflowing_add<uint_multiprecision_t>(7ull, 5ull);
    const auto r4 = overflowing_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 1ull);
    const auto r5 = overflowing_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 0ull);
    const auto r6 =
        overflowing_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max() - 1ull, 1ull);

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_EQ(r1.overflow, false);
    EXPECT_EQ(r2.value, 3ull);
    EXPECT_EQ(r2.overflow, false);
    EXPECT_EQ(r3.value, 12ull);
    EXPECT_EQ(r3.overflow, false);
    EXPECT_EQ(r4.value, 0ull);
    EXPECT_EQ(r4.overflow, true);
    EXPECT_EQ(r5.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(r5.overflow, false);
    EXPECT_EQ(r6.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(r6.overflow, false);
}

TEST(WideOps, OverflowingSubInt) {
    using beman::big_int::detail::overflowing_sub;

    const auto r1 = overflowing_sub<int_multiprecision_t>(0, 0);
    const auto r2 = overflowing_sub<int_multiprecision_t>(7, 5);
    const auto r3 = overflowing_sub<int_multiprecision_t>(-7, -5);
    const auto r4 = overflowing_sub<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), 1);
    const auto r5 = overflowing_sub<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::max(), -1);
    const auto r6 = overflowing_sub<int_multiprecision_t>(std::numeric_limits<int_multiprecision_t>::min(), -1);

    EXPECT_EQ(r1.value, static_cast<int_multiprecision_t>(0));
    EXPECT_EQ(r1.overflow, false);
    EXPECT_EQ(r2.value, static_cast<int_multiprecision_t>(2));
    EXPECT_EQ(r2.overflow, false);
    EXPECT_EQ(r3.value, static_cast<int_multiprecision_t>(-2));
    EXPECT_EQ(r3.overflow, false);
    EXPECT_EQ(r4.value, std::numeric_limits<int_multiprecision_t>::max());
    EXPECT_EQ(r4.overflow, true);
    EXPECT_EQ(r5.value, std::numeric_limits<int_multiprecision_t>::min());
    EXPECT_EQ(r5.overflow, true);
    EXPECT_EQ(r6.value, std::numeric_limits<int_multiprecision_t>::min() + 1);
    EXPECT_EQ(r6.overflow, false);
}

TEST(WideOps, OverflowingSubUint) {
    using beman::big_int::detail::overflowing_sub;

    const auto r1 = overflowing_sub<uint_multiprecision_t>(0ull, 0ull);
    const auto r2 = overflowing_sub<uint_multiprecision_t>(7ull, 5ull);
    const auto r3 = overflowing_sub<uint_multiprecision_t>(0ull, 1ull);
    const auto r4 = overflowing_sub<uint_multiprecision_t>(1ull, 0ull);
    const auto r5 = overflowing_sub<uint_multiprecision_t>(1ull, 1ull);
    const auto r6 = overflowing_sub<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(),
                                                           std::numeric_limits<uint_multiprecision_t>::max());

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_EQ(r1.overflow, false);
    EXPECT_EQ(r2.value, 2ull);
    EXPECT_EQ(r2.overflow, false);
    EXPECT_EQ(r3.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(r3.overflow, true);
    EXPECT_EQ(r4.value, 1ull);
    EXPECT_EQ(r4.overflow, false);
    EXPECT_EQ(r5.value, 0ull);
    EXPECT_EQ(r5.overflow, false);
    EXPECT_EQ(r6.value, 0ull);
    EXPECT_EQ(r6.overflow, false);
}

TEST(WideOps, OverflowingMulUint) {
    using beman::big_int::detail::overflowing_mul;

    const auto r1 = overflowing_mul<uint_multiprecision_t>(0ull, 7ull);
    const auto r2 = overflowing_mul<uint_multiprecision_t>(1ull, 1ull);
    const auto r3 = overflowing_mul<uint_multiprecision_t>(7ull, 5ull);
    const auto r4 = overflowing_mul<uint_multiprecision_t>(1ull << 32, 1ull << 32);
    const auto r5 = overflowing_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 2ull);
    const auto r6 = overflowing_mul<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(),
                                                           std::numeric_limits<uint_multiprecision_t>::max());

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_EQ(r1.overflow, false);
    EXPECT_EQ(r2.value, 1ull);
    EXPECT_EQ(r2.overflow, false);
    EXPECT_EQ(r3.value, 35ull);
    EXPECT_EQ(r3.overflow, false);
    EXPECT_EQ(r4.value, 0ull);
    EXPECT_EQ(r4.overflow, true);
    EXPECT_EQ(r5.value, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_EQ(r5.overflow, true);
    EXPECT_EQ(r6.value, 1ull);
    EXPECT_EQ(r6.overflow, true);
}

TEST(WideOps, CarryingAddUint) {
    using beman::big_int::detail::carrying_add;

    const auto r1 = carrying_add<uint_multiprecision_t>(0ull, 0ull, false);
    const auto r2 = carrying_add<uint_multiprecision_t>(1ull, 2ull, false);
    const auto r3 = carrying_add<uint_multiprecision_t>(std::numeric_limits<uint_multiprecision_t>::max(), 0ull, true);
    const auto r4 = carrying_add<uint_multiprecision_t>(
        std::numeric_limits<uint_multiprecision_t>::max(), std::numeric_limits<uint_multiprecision_t>::max(), false);
    const auto r5 = carrying_add<uint_multiprecision_t>(
        std::numeric_limits<uint_multiprecision_t>::max(), std::numeric_limits<uint_multiprecision_t>::max(), true);
    const auto r6 = carrying_add<uint_multiprecision_t>(42ull, 58ull, false);

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_EQ(r1.carry, false);
    EXPECT_EQ(r2.value, 3ull);
    EXPECT_EQ(r2.carry, false);
    EXPECT_EQ(r3.value, 0ull);
    EXPECT_EQ(r3.carry, true);
    EXPECT_EQ(r4.value, std::numeric_limits<uint_multiprecision_t>::max() - 1ull);
    EXPECT_EQ(r4.carry, true);
    EXPECT_EQ(r5.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(r5.carry, true);
    EXPECT_EQ(r6.value, 100ull);
    EXPECT_EQ(r6.carry, false);
}

TEST(WideOps, BorrowingSubUint) {
    using beman::big_int::detail::borrowing_sub;

    const auto r1 = borrowing_sub<uint_multiprecision_t>(0ull, 0ull, false);
    const auto r2 = borrowing_sub<uint_multiprecision_t>(5ull, 3ull, false);
    const auto r3 = borrowing_sub<uint_multiprecision_t>(0ull, 0ull, true);
    const auto r4 = borrowing_sub<uint_multiprecision_t>(0ull, 1ull, false);
    const auto r5 = borrowing_sub<uint_multiprecision_t>(1ull, 1ull, false);
    const auto r6 = borrowing_sub<uint_multiprecision_t>(100ull, 58ull, false);

    EXPECT_EQ(r1.value, 0ull);
    EXPECT_EQ(r1.borrow, false);
    EXPECT_EQ(r2.value, 2ull);
    EXPECT_EQ(r2.borrow, false);
    EXPECT_EQ(r3.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(r3.borrow, true);
    EXPECT_EQ(r4.value, std::numeric_limits<uint_multiprecision_t>::max());
    EXPECT_EQ(r4.borrow, true);
    EXPECT_EQ(r5.value, 0ull);
    EXPECT_EQ(r5.borrow, false);
    EXPECT_EQ(r6.value, 42ull);
    EXPECT_EQ(r6.borrow, false);
}

TEST(WideOps, NarrowingDivUint) {
    using beman::big_int::detail::narrowing_div;
    using beman::big_int::detail::wide;

    const auto r1 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 5ull, .high_bits = 1ull}, 3ull);
    const auto r2 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 20ull, .high_bits = 0ull}, 6ull);
    const auto r3 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 0ull, .high_bits = 0ull}, 7ull);
    const auto r4 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 1ull, .high_bits = 0ull}, 1ull);
    const auto r5 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 13ull, .high_bits = 1ull}, 5ull);
    const auto r6 = narrowing_div(wide<uint_multiprecision_t>{.low_bits = 14ull, .high_bits = 1ull}, 5ull);

    EXPECT_EQ(r1.quotient, 6'148'914'691'236'517'207ull);
    EXPECT_EQ(r1.remainder, 0ull);
    EXPECT_EQ(r2.quotient, 3ull);
    EXPECT_EQ(r2.remainder, 2ull);
    EXPECT_EQ(r3.quotient, 0ull);
    EXPECT_EQ(r3.remainder, 0ull);
    EXPECT_EQ(r4.quotient, 1ull);
    EXPECT_EQ(r4.remainder, 0ull);
    EXPECT_EQ(r5.quotient, 3'689'348'814'741'910'325ull);
    EXPECT_EQ(r5.remainder, 4ull);
    EXPECT_EQ(r6.quotient, 3'689'348'814'741'910'326ull);
    EXPECT_EQ(r6.remainder, 0ull);
}

} // namespace
