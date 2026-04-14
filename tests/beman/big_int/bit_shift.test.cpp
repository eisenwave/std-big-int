// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <limits>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

namespace {

using beman::big_int::basic_big_int;
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

    EXPECT_EQ(x.representation().size(), 3U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(x.representation()[2], 1ULL);
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
    big_int z{std::numeric_limits<uint_multiprecision_t>::max()};
    z = -z;

    x >>= 1;
    y >>= 1;
    z >>= 64;

    EXPECT_EQ(x == -2, true);
    EXPECT_EQ(y == -2, true);
    EXPECT_EQ(z == -1, true);
}

TEST(BitShift, LeftShiftZeroRemainsZero) {
    big_int x{0};

    x <<= 1;
    x <<= 64;
    x <<= 129;

    EXPECT_EQ(x == 0, true);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation().size() >= 1U, true);
    EXPECT_EQ(x.representation()[x.representation().size() - 1], 0ULL);
}

TEST(BitShift, LeftShiftWholeLimbPreservesMagnitudeDigits) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 64;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], std::numeric_limits<uint_multiprecision_t>::max());

    x <<= 64;

    EXPECT_EQ(x.representation().size(), 3U);
    EXPECT_EQ(x.representation()[0], 0ULL);
    EXPECT_EQ(x.representation()[1], 0ULL);
    EXPECT_EQ(x.representation()[2], std::numeric_limits<uint_multiprecision_t>::max());
}

TEST(BitShift, RightShiftCrossLimbBitPropagation) {
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};

    x <<= 64;
    x >>= 1;

    EXPECT_EQ(x.representation().size(), 2U);
    EXPECT_EQ(x.representation()[0], 0x8000'0000'0000'0000ULL);
    EXPECT_EQ(x.representation()[1], 0x7FFF'FFFF'FFFF'FFFFULL);
}

TEST(BitShift, RightShiftLargePositiveBecomesZero) {
    big_int x{1};

    x >>= 64;
    EXPECT_EQ(x == 0, true);
    EXPECT_EQ(x.representation().size(), 1U);
    EXPECT_EQ(x.representation()[0], 0ULL);

    x = 1;
    x >>= 257;
    EXPECT_EQ(x == 0, true);
    EXPECT_EQ(x.representation().size(), 1U);
    EXPECT_EQ(x.representation()[0], 0ULL);
}

TEST(BitShift, RightShiftLargeNegativeBecomesMinusOne) {
    big_int x{-1};
    big_int y{-2};

    x >>= 64;
    y >>= 64;

    EXPECT_EQ(x == -1, true);
    EXPECT_EQ(y == -1, true);

    y = -2;
    y >>= 257;
    EXPECT_EQ(y == -1, true);
}

TEST(BitShift, LeftThenRightRoundTripForPositive) {
    big_int x{123'456'789};

    x <<= 17;
    x >>= 17;
    EXPECT_EQ(x == 123'456'789, true);

    x <<= 64;
    x >>= 64;
    EXPECT_EQ(x == 123'456'789, true);

    x <<= 130;
    x >>= 130;
    EXPECT_EQ(x == 123'456'789, true);
}

// ----- operator<< (non-mutating left shift) -----

TEST(BitShift, OperatorLeftShiftBasic) {
    const big_int x{1};
    const big_int r = x << 1;
    EXPECT_EQ(r, 2);
    EXPECT_EQ(x, 1); // original unchanged
}

TEST(BitShift, OperatorLeftShiftZeroShift) {
    const big_int x{42};
    const big_int r = x << 0;
    EXPECT_EQ(r, 42);
}

TEST(BitShift, OperatorLeftShiftZeroValue) {
    const big_int x{0};
    const big_int r = x << 100;
    EXPECT_EQ(r, 0);
}

TEST(BitShift, OperatorLeftShiftNegative) {
    const big_int x{-1};
    const big_int r = x << 1;
    EXPECT_EQ(r, -2);

    const big_int y{-3};
    const big_int s = y << 2;
    EXPECT_EQ(s, -12);
}

TEST(BitShift, OperatorLeftShiftMatchesCompound) {
    // operator<< on an lvalue should produce the same result as <<=
    const big_int x{123'456'789};
    big_int       y{123'456'789};

    const big_int r = x << 17;
    y <<= 17;
    EXPECT_EQ(r, y);

    const big_int a{std::numeric_limits<uint_multiprecision_t>::max()};
    big_int       b{std::numeric_limits<uint_multiprecision_t>::max()};

    const big_int r2 = a << 65;
    b <<= 65;
    EXPECT_EQ(r2, b);
}

TEST(BitShift, OperatorLeftShiftRvalue) {
    // rvalue path: should move the storage, no copy
    big_int r = big_int{1} << 10;
    EXPECT_EQ(r, 1024);

    big_int s = big_int{std::numeric_limits<uint_multiprecision_t>::max()} << 1;
    EXPECT_EQ(s.representation().size(), 2U);
    EXPECT_EQ(s.representation()[0], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(s.representation()[1], 1ULL);
}

TEST(BitShift, OperatorLeftShiftInplaceToHeap) {
    // default big_int has inplace_capacity=1 (one 64-bit limb).
    // Shifting 1 left by 63 bits: top limb = 0x8000...0, CLZ = 0.
    // A further shift by 1 bit must overflow into a second limb (heap).
    const big_int x{1ULL << 63};
    EXPECT_EQ(x.capacity(), 0U); // inline

    const big_int r = x << 1;
    EXPECT_EQ(r.representation().size(), 2U);
    EXPECT_EQ(r.representation()[0], 0ULL);
    EXPECT_EQ(r.representation()[1], 1ULL);
}

TEST(BitShift, OperatorLeftShiftInplaceStaysInlineCLZ) {
    // Value 1 has CLZ = 63 in a 64-bit limb. Shifting left by up to 63 bits
    // should NOT overflow into a new limb, staying in inline storage.
    const big_int x{1};
    EXPECT_EQ(x.capacity(), 0U); // inline

    const big_int r = x << 63;
    EXPECT_EQ(r.capacity(), 0U); // still inline
    EXPECT_EQ(r.representation().size(), 1U);
    EXPECT_EQ(r.representation()[0], 1ULL << 63);
}

TEST(BitShift, OperatorLeftShiftWiderInplaceStaysInline) {
    // basic_big_int<256> has 4 inline limbs (256 bits). A small value shifted
    // within those 256 bits should never touch the heap.
    using big_int_256 = basic_big_int<256>;
    const big_int_256 x{1};
    EXPECT_EQ(x.capacity(), 0U);

    const big_int_256 r = x << 255;
    EXPECT_EQ(r.capacity(), 0U); // still inline

    // Top limb should be 1 << (255 % 64) = 1 << 63
    EXPECT_EQ(r.representation().size(), 4U);
    EXPECT_EQ(r.representation()[3], 1ULL << 63);
    EXPECT_EQ(r.representation()[2], 0ULL);
    EXPECT_EQ(r.representation()[1], 0ULL);
    EXPECT_EQ(r.representation()[0], 0ULL);
}

TEST(BitShift, OperatorLeftShiftWiderInplaceToHeap) {
    // basic_big_int<256>: 4 inline limbs. Shifting 1 by 256 bits requires
    // 5 limbs, which must spill to the heap.
    using big_int_256 = basic_big_int<256>;
    const big_int_256 x{1};

    const big_int_256 r = x << 256;
    EXPECT_EQ(r.representation().size(), 5U);
    EXPECT_NE(r.capacity(), 0U); // heap
    EXPECT_EQ(r.representation()[4], 1ULL);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(r.representation()[static_cast<std::size_t>(i)], 0ULL);
    }
}

TEST(BitShift, OperatorLeftShiftAcrossLimbs) {
    const big_int x{1};

    const big_int r64  = x << 64;
    const big_int r65  = x << 65;
    const big_int r128 = x << 128;

    EXPECT_EQ(r64.representation().size(), 2U);
    EXPECT_EQ(r64.representation()[0], 0ULL);
    EXPECT_EQ(r64.representation()[1], 1ULL);

    EXPECT_EQ(r65.representation().size(), 2U);
    EXPECT_EQ(r65.representation()[0], 0ULL);
    EXPECT_EQ(r65.representation()[1], 2ULL);

    EXPECT_EQ(r128.representation().size(), 3U);
    EXPECT_EQ(r128.representation()[0], 0ULL);
    EXPECT_EQ(r128.representation()[1], 0ULL);
    EXPECT_EQ(r128.representation()[2], 1ULL);
}

TEST(BitShift, OperatorLeftShiftHuge) {
    // Shift a small value by a large amount (10000 bits).
    const big_int x{1};
    const big_int r = x << 10000;

    // 10000 / 64 = 156 whole limbs, 10000 % 64 = 16-bit shift
    EXPECT_EQ(r.representation().size(), 157U);
    EXPECT_EQ(r.representation()[156], 1ULL << 16);
    EXPECT_EQ(r.representation()[155], 0ULL);
    EXPECT_EQ(r.representation()[0], 0ULL);

    // Verify via round-trip with >>=
    big_int rt = r;
    rt >>= 10000;
    EXPECT_EQ(rt, 1);
}

TEST(BitShift, OperatorLeftShiftHeapSource) {
    // Source already on the heap; operator<< should pre-allocate headroom
    // so shift_left doesn't reallocate.
    big_int x{std::numeric_limits<uint_multiprecision_t>::max()};
    x <<= 1; // now 2 limbs, on heap
    EXPECT_NE(x.capacity(), 0U);

    const big_int r = x << 64;
    EXPECT_EQ(r.representation().size(), 3U);
    EXPECT_EQ(r.representation()[0], 0ULL);
    EXPECT_EQ(r.representation()[1], std::numeric_limits<uint_multiprecision_t>::max() - 1ULL);
    EXPECT_EQ(r.representation()[2], 1ULL);
}

TEST(BitShift, OperatorLeftShiftSignedShiftAmount) {
    const big_int x{1};
    const big_int r = x << static_cast<int>(10);
    EXPECT_EQ(r, 1024);
}

TEST(BitShift, OperatorLeftShiftPreservesOriginal) {
    // Verify operator<< doesn't modify the source even when the source
    // is large and heap-allocated.
    const big_int x = big_int{1} << 200;
    const auto    x_rep = x.representation();

    const big_int r = x << 50;
    EXPECT_EQ(x.representation().size(), x_rep.size());
    for (std::size_t i = 0; i < x_rep.size(); ++i) {
        EXPECT_EQ(x.representation()[i], x_rep[i]);
    }

    // And the result should match doing it in two steps
    big_int expected{1};
    expected <<= 250;
    EXPECT_EQ(r, expected);
}

// ----- operator>> (non-mutating right shift) -----

TEST(BitShift, OperatorRightShiftBasic) {
    const big_int x{4};
    const big_int r = x >> 1;
    EXPECT_EQ(r, 2);
    EXPECT_EQ(x, 4); // original unchanged
}

TEST(BitShift, OperatorRightShiftZeroShift) {
    const big_int x{42};
    const big_int r = x >> 0;
    EXPECT_EQ(r, 42);
}

TEST(BitShift, OperatorRightShiftZeroValue) {
    const big_int x{0};
    const big_int r = x >> 100;
    EXPECT_EQ(r, 0);
}

TEST(BitShift, OperatorRightShiftToZero) {
    const big_int x{1};
    const big_int r = x >> 1;
    EXPECT_EQ(r, 0);

    const big_int s = x >> 64;
    EXPECT_EQ(s, 0);

    const big_int t = x >> 1000;
    EXPECT_EQ(t, 0);
}

TEST(BitShift, OperatorRightShiftNegativeRounding) {
    // Right shift of negative values rounds toward negative infinity.
    const big_int x{-3};
    const big_int r = x >> 1;
    EXPECT_EQ(r, -2);

    const big_int y{-4};
    const big_int s = y >> 1;
    EXPECT_EQ(s, -2);

    const big_int w{-1};
    const big_int t = w >> 64;
    EXPECT_EQ(t, -1);
}

TEST(BitShift, OperatorRightShiftMatchesCompound) {
    const big_int x = big_int{1} << 200;
    big_int       y = big_int{1} << 200;

    const big_int r = x >> 130;
    y >>= 130;
    EXPECT_EQ(r, y);

    big_int expected{1};
    expected <<= 70;
    EXPECT_EQ(r, expected);
}

TEST(BitShift, OperatorRightShiftRvalue) {
    big_int r = big_int{1024} >> 5;
    EXPECT_EQ(r, 32);

    // Rvalue from a heap source
    big_int r2 = (big_int{1} << 128) >> 64;
    big_int expected{1};
    expected <<= 64;
    EXPECT_EQ(r2, expected);
}

TEST(BitShift, OperatorRightShiftAcrossLimbs) {
    const big_int x = big_int{1} << 128; // 3 limbs
    EXPECT_EQ(x.representation().size(), 3U);

    const big_int r = x >> 64;
    EXPECT_EQ(r.representation().size(), 2U);
    EXPECT_EQ(r.representation()[0], 0ULL);
    EXPECT_EQ(r.representation()[1], 1ULL);

    const big_int s = x >> 128;
    EXPECT_EQ(s, 1);
}

TEST(BitShift, OperatorRightShiftKeepsHeapStorage) {
    // When the source is on the heap and the result could fit inline,
    // operator>> should keep the heap buffer (no shrink_to_fit).
    big_int x{1};
    x <<= 128; // 3 limbs, on heap
    EXPECT_NE(x.capacity(), 0U);

    const big_int r = x >> 200; // result is 0, but copied from heap source
    EXPECT_EQ(r, 0);
    // The copy via assign_value inherits the heap buffer
    EXPECT_NE(r.capacity(), 0U);
}

TEST(BitShift, OperatorRightShiftPreservesOriginal) {
    const big_int x = big_int{1} << 200;
    const auto    x_rep = x.representation();

    const big_int r = x >> 50;
    // Original unchanged
    EXPECT_EQ(x.representation().size(), x_rep.size());
    for (std::size_t i = 0; i < x_rep.size(); ++i) {
        EXPECT_EQ(x.representation()[i], x_rep[i]);
    }

    big_int expected{1};
    expected <<= 150;
    EXPECT_EQ(r, expected);
}

TEST(BitShift, OperatorRightShiftSignedShiftAmount) {
    const big_int x{1024};
    const big_int r = x >> static_cast<int>(5);
    EXPECT_EQ(r, 32);
}

TEST(BitShift, OperatorLeftRightRoundTrip) {
    // Non-mutating round trip: (x << n) >> n == x for positive values
    const big_int x{123'456'789};

    EXPECT_EQ((x << 17) >> 17, x);
    EXPECT_EQ((x << 64) >> 64, x);
    EXPECT_EQ((x << 130) >> 130, x);
}

} // namespace
