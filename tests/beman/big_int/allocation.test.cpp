// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

// ----- compile-time tests -----

consteval bool test_size_default() {
    beman::big_int::big_int x;
    return x.size() == 0; // size() for zero returns 0 (consistent with D4444)
}
static_assert(test_size_default());

consteval bool test_size_from_value() {
    beman::big_int::big_int x{42U};
    return x.size() == 6; // size() returns msb + 1
}
static_assert(test_size_from_value());

consteval bool test_size_from_value_neg() {
    beman::big_int::big_int x{-42};
    return x.size() == 6; // size() for x negative returns (-x).size()
}
static_assert(test_size_from_value_neg());

consteval bool test_size_from_value_big() {
    using namespace beman::big_int::literals;
    beman::big_int::big_int x{
        31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    return x.size() == 3324; // size() returns msb + 1
}
static_assert(test_size_from_value_big());

consteval bool test_size_from_value_big_neg() {
    using namespace beman::big_int::literals;
    beman::big_int::big_int x{
        -31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    return x.size() == 3324; // size() for x negative returns (-x).size()
}
static_assert(test_size_from_value_big_neg());

consteval bool test_max_size() {
    return beman::big_int::big_int::max_size() == (std::numeric_limits<std::uint32_t>::max() >> 1U);
}
static_assert(test_max_size());

consteval bool test_capacity_default() {
    beman::big_int::big_int x;
    return x.capacity() == 0; // inline storage => capacity 0
}
static_assert(test_capacity_default());

consteval bool test_reserve_within_inline() {
    beman::big_int::big_int x;
    x.reserve(1); // fits in inline storage, should be a no-op
    return x.capacity() == 0;
}
static_assert(test_reserve_within_inline());

consteval bool test_reserve_beyond_inline() {
    beman::big_int::big_int x;
    x.reserve(4);
    return x.capacity() >= 4;
}
static_assert(test_reserve_beyond_inline());

consteval bool test_reserve_preserves_value() {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    return x.representation()[0] == 42U && x.capacity() >= 8;
}
static_assert(test_reserve_preserves_value());

consteval bool test_reserve_doubling() {
    beman::big_int::big_int x;
    x.reserve(3); // first allocation: max(3, 1) = 3
    return x.capacity() >= 3;
}
static_assert(test_reserve_doubling());

consteval bool test_reserve_grows_geometrically() {
    beman::big_int::big_int x;
    x.reserve(4); // cap = max(4, 1)   = 4
    x.reserve(5); // cap = max(5, 2*4) = 8
    return x.capacity() == 8;
}
static_assert(test_reserve_grows_geometrically());

consteval bool test_reserve_no_shrink() {
    beman::big_int::big_int x;
    x.reserve(10);
    auto cap = x.capacity();
    x.reserve(2); // should not shrink
    return x.capacity() == cap;
}
static_assert(test_reserve_no_shrink());

consteval bool test_shrink_to_fit_noop_inline() {
    beman::big_int::big_int x;
    x.shrink_to_fit(); // no-op on inline storage
    return x.capacity() == 0;
}
static_assert(test_shrink_to_fit_noop_inline());

// ----- representation_size / max_representation_size / representation_capacity / reserve_representation -----

consteval bool test_representation_size_zero() {
    beman::big_int::big_int x;
    // A zero value occupies a single limb, matching representation().size().
    return x.representation_size() == 1U && x.representation_size() == x.representation().size();
}
static_assert(test_representation_size_zero());

consteval bool test_representation_size_small() {
    beman::big_int::big_int x{42U};
    return x.representation_size() == 1U && x.representation_size() == x.representation().size();
}
static_assert(test_representation_size_small());

consteval bool test_representation_size_negative() {
    // The magnitude, not the sign, determines representation_size().
    beman::big_int::big_int pos{42U};
    beman::big_int::big_int neg{-42};
    return neg.representation_size() == pos.representation_size();
}
static_assert(test_representation_size_negative());

consteval bool test_representation_size_matches_formula() {
    using namespace beman::big_int::literals;
    beman::big_int::big_int x{18446744073709551616_n}; // 2^64, size() == 65
    constexpr std::size_t   digits =
        static_cast<std::size_t>(std::numeric_limits<beman::big_int::uint_multiprecision_t>::digits);
    const std::size_t expected = (x.size() + digits - 1U) / digits; // ceil(size() / digits)
    return x.representation_size() == expected && x.representation_size() == x.representation().size() &&
           x.representation_size() >= 2U;
}
static_assert(test_representation_size_matches_formula());

consteval bool test_max_representation_size() {
    // Limb-count limit; identical to max_size() in this limb-counted implementation.
    return beman::big_int::big_int::max_representation_size() == beman::big_int::big_int::max_size();
}
static_assert(test_max_representation_size());

consteval bool test_representation_capacity_inline() {
    beman::big_int::big_int x;
    // In-place storage: capacity() reports 0, but representation_capacity() reports the
    // in-place limb count.
    return x.capacity() == 0U && x.representation_capacity() == beman::big_int::big_int::inplace_capacity;
}
static_assert(test_representation_capacity_inline());

consteval bool test_representation_capacity_heap() {
    beman::big_int::big_int x;
    x.reserve_representation(8);
    // Once on the heap, representation_capacity() tracks capacity().
    return x.capacity() >= 8U && x.representation_capacity() == x.capacity();
}
static_assert(test_representation_capacity_heap());

consteval bool test_reserve_representation_within_inline() {
    beman::big_int::big_int x;
    x.reserve_representation(1); // fits in-place, no allocation
    return x.capacity() == 0U && x.representation_capacity() >= 1U;
}
static_assert(test_reserve_representation_within_inline());

consteval bool test_reserve_representation_beyond_inline() {
    beman::big_int::big_int x;
    x.reserve_representation(4);
    return x.capacity() >= 4U && x.representation_capacity() >= 4U;
}
static_assert(test_reserve_representation_beyond_inline());

consteval bool test_reserve_representation_preserves_value() {
    beman::big_int::big_int x{42U};
    x.reserve_representation(8);
    return x.representation()[0] == 42U && x.representation_capacity() >= 8U;
}
static_assert(test_reserve_representation_preserves_value());

// ----- runtime tests -----

TEST(Allocation, SizeDefault) {
    beman::big_int::big_int x;
    EXPECT_EQ(x.size(), 0);
}

TEST(Allocation, SizeFromValue) {
    beman::big_int::big_int x{42U};
    EXPECT_EQ(x.size(), 6);
}

TEST(Allocation, SizeFromValueNeg) {
    beman::big_int::big_int x{-42};
    EXPECT_EQ(x.size(), 6);
}

TEST(Allocation, SizeFromValueBig) {
    using namespace beman::big_int::literals;
    beman::big_int::big_int x{
        31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    EXPECT_EQ(x.size(), 3324);
}

TEST(Allocation, SizeFromValueBigNeg) {
    using namespace beman::big_int::literals;
    beman::big_int::big_int x{
        -31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    EXPECT_EQ(x.size(), 3324);
}

TEST(Allocation, MaxSize) {
    EXPECT_EQ(beman::big_int::big_int::max_size(), std::numeric_limits<std::uint32_t>::max() >> 1U);
}

TEST(Allocation, CapacityDefault) {
    beman::big_int::big_int x;
    EXPECT_EQ(x.capacity(), 0U);
}

TEST(Allocation, ReserveWithinInline) {
    beman::big_int::big_int x;
    x.reserve(1);
    EXPECT_EQ(x.capacity(), 0U);
}

TEST(Allocation, ReserveBeyondInline) {
    beman::big_int::big_int x;
    x.reserve(4);
    EXPECT_GE(x.capacity(), 4U);
}

TEST(Allocation, ReservePreservesValue) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    EXPECT_EQ(x.representation()[0], 42U);
    EXPECT_GE(x.capacity(), 8U);
}

TEST(Allocation, ReserveDoubling) {
    beman::big_int::big_int x;
    x.reserve(3); // max(3, 2*2) = 4
    EXPECT_GE(x.capacity(), 3);
}

TEST(Allocation, ReserveGrowsGeometrically) {
    beman::big_int::big_int x;
    x.reserve(4); // cap = 4
    EXPECT_GE(x.capacity(), 4u);
    x.reserve(5); // cap = max(5, 2*4) = 8
    EXPECT_GE(x.capacity(), 8u);
}

TEST(Allocation, ReserveNoShrink) {
    beman::big_int::big_int x;
    x.reserve(10);
    auto cap = x.capacity();
    x.reserve(2);
    EXPECT_EQ(x.capacity(), cap);
}

TEST(Allocation, ShrinkToFitNoopInline) {
    beman::big_int::big_int x;
    x.shrink_to_fit();
    EXPECT_EQ(x.capacity(), 0U);
}

TEST(Allocation, ShrinkToFitAfterReserve) {
    beman::big_int::big_int x{42U};
    x.reserve(16);
    EXPECT_GE(x.capacity(), 16U);
    x.shrink_to_fit();
    // After shrink, capacity should be reduced
    EXPECT_LT(x.capacity(), 16U);
    // Value should be preserved
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(Allocation, ReserveLargeValue) {
    beman::big_int::big_int x;
    x.reserve(1024);
    EXPECT_GE(x.capacity(), 1024U);
}

TEST(Allocation, RepresentationSizeZero) {
    beman::big_int::big_int x;
    EXPECT_EQ(x.representation_size(), 1U);
    EXPECT_EQ(x.representation_size(), x.representation().size());
}

TEST(Allocation, RepresentationSizeSmall) {
    beman::big_int::big_int x{42U};
    EXPECT_EQ(x.representation_size(), 1U);
    EXPECT_EQ(x.representation_size(), x.representation().size());
}

TEST(Allocation, RepresentationSizeNegativeMatchesMagnitude) {
    beman::big_int::big_int pos{42};
    beman::big_int::big_int neg{-42};
    EXPECT_EQ(neg.representation_size(), pos.representation_size());
}

TEST(Allocation, RepresentationSizeBig) {
    using namespace beman::big_int::literals;
    beman::big_int::big_int x{
        31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};
    EXPECT_EQ(x.representation_size(), x.representation().size());
    EXPECT_GT(x.representation_size(), 1U);
}

TEST(Allocation, MaxRepresentationSize) {
    EXPECT_EQ(beman::big_int::big_int::max_representation_size(), beman::big_int::big_int::max_size());
}

TEST(Allocation, RepresentationCapacityInline) {
    beman::big_int::big_int x;
    constexpr std::size_t   inplace_cap = beman::big_int::big_int::inplace_capacity;
    EXPECT_EQ(x.capacity(), 0U);
    EXPECT_EQ(x.representation_capacity(), inplace_cap);
}

TEST(Allocation, ReserveRepresentationBeyondInline) {
    beman::big_int::big_int x;
    x.reserve_representation(4);
    EXPECT_GE(x.capacity(), 4U);
    EXPECT_GE(x.representation_capacity(), 4U);
    EXPECT_EQ(x.representation_capacity(), x.capacity());
}

TEST(Allocation, ReserveRepresentationPreservesValue) {
    beman::big_int::big_int x{42U};
    x.reserve_representation(8);
    EXPECT_EQ(x.representation()[0], 42U);
    EXPECT_GE(x.representation_capacity(), 8U);
}

// ----- copy/move with heap storage -----

TEST(Allocation, CopyConstructHeapAllocated) {
    beman::big_int::big_int x{42U};
    EXPECT_EQ(x.representation().size(), 1);
    x.reserve(8); // force heap
    // GE instead of EQ because allocate_at_least may be used.
    EXPECT_GE(x.capacity(), 8);
    EXPECT_EQ(x.representation().size(), 1);

    beman::big_int::big_int y(x);
    // y should have no heap allocation
    // because the integer value can be represented using a single limb,
    // irrespective of what the capacity of x is.
    EXPECT_EQ(y.capacity(), 0);
    EXPECT_EQ(y.representation().size(), 1);
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, MoveConstructHeapAllocated) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    auto                    cap = x.capacity();
    beman::big_int::big_int y(std::move(x));
    EXPECT_EQ(y.representation()[0], 42U);
    EXPECT_EQ(y.capacity(), cap);
}

TEST(Allocation, CopyAssignHeapToInline) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    beman::big_int::big_int y;
    y = x;
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, CopyAssignHeapToHeap) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    beman::big_int::big_int y{99U};
    y.reserve(4);
    y = x;
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, MoveAssignHeapToInline) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    beman::big_int::big_int y;
    y = std::move(x);
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, MoveAssignHeapToHeap) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    beman::big_int::big_int y{99U};
    y.reserve(4);
    y = std::move(x);
    EXPECT_EQ(y.representation()[0], 42U);
}

TEST(Allocation, SelfAssignment) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    auto& ref = x;
    x         = ref;
    EXPECT_EQ(x.representation()[0], 42U);
}

// ----- operator= storage reuse -----
// The shared `assign_value` helper keeps the destination's allocation if its
// effective capacity already fits the source. These tests verify the fast path
// by checking that the destination's data pointer does not change.

TEST(Allocation, CopyAssignReusesDstStorage) {
    beman::big_int::big_int dst{1U};
    dst.reserve(8); // dst now on the heap with capacity >= 8
    const auto* const dst_data = dst.representation().data();
    const auto        dst_cap  = dst.capacity();

    const beman::big_int::big_int src = beman::big_int::big_int{0xFFFFFFFFFFFFFFFFU} + beman::big_int::big_int{1};
    ASSERT_EQ(src.representation().size(), 2U); // heap, 2 limbs -- fits in dst's capacity

    dst = src;
    EXPECT_EQ(dst.representation().data(), dst_data); // no reallocation
    EXPECT_EQ(dst.capacity(), dst_cap);
    ASSERT_EQ(dst.representation().size(), 2U);
    EXPECT_EQ(dst, src);
}

TEST(Allocation, MoveAssignReusesDstStorageWhenLarger) {
    // When dst's capacity already covers src's limb count, assign_value should
    // copy src's limbs into dst's buffer rather than stealing src's (smaller)
    // buffer.
    beman::big_int::big_int dst{1U};
    dst.reserve(16); // big dst buffer
    const auto* const dst_data = dst.representation().data();
    const auto        dst_cap  = dst.capacity();

    beman::big_int::big_int src = beman::big_int::big_int{0xFFFFFFFFFFFFFFFFU} + beman::big_int::big_int{1};
    ASSERT_EQ(src.representation().size(), 2U);
    const auto src_cap = src.capacity();
    ASSERT_LT(src_cap, dst_cap); // dst has more capacity than src

    dst = std::move(src);
    // dst retained its larger buffer rather than adopting src's smaller one.
    EXPECT_EQ(dst.representation().data(), dst_data);
    EXPECT_EQ(dst.capacity(), dst_cap);
    ASSERT_EQ(dst.representation().size(), 2U);
}

TEST(Allocation, MoveAssignStealsSrcWhenDstTooSmall) {
    // When dst's capacity is insufficient, move-assign must steal src's buffer
    // (noexcept contract -- no allocation allowed).
    beman::big_int::big_int dst; // inline, capacity 0
    EXPECT_EQ(dst.capacity(), 0U);

    beman::big_int::big_int src = beman::big_int::big_int{0xFFFFFFFFFFFFFFFFU} + beman::big_int::big_int{1};
    ASSERT_GT(src.capacity(), 0U);
    const auto* const src_data = src.representation().data();
    const auto        src_cap  = src.capacity();

    dst = std::move(src);
    // dst adopted src's buffer wholesale.
    EXPECT_EQ(dst.representation().data(), src_data);
    EXPECT_EQ(dst.capacity(), src_cap);
    // src released heap ownership (moved-from state; value is unspecified,
    // matching the existing move-assign contract).
    EXPECT_EQ(src.capacity(), 0U);
}

TEST(Allocation, CopyAssignAllocatesWhenDstTooSmall) {
    // When dst has no (heap) capacity and src is bigger than inline, copy-assign
    // must allocate a fresh buffer.
    beman::big_int::big_int       dst; // inline, capacity 0
    const beman::big_int::big_int src = beman::big_int::big_int{0xFFFFFFFFFFFFFFFFU} + beman::big_int::big_int{1};
    ASSERT_GT(src.capacity(), 0U);

    dst = src;
    ASSERT_EQ(dst.representation().size(), 2U);
    EXPECT_GT(dst.capacity(), 0U);
    EXPECT_NE(dst.representation().data(), src.representation().data());
    EXPECT_EQ(dst, src);
}

TEST(Allocation, AssignPreservesInlineBitCastInvariant) {
    // After assigning a shorter value into a destination that previously held
    // a longer value in inline storage, the unused tail limbs must be zero so
    // that `inplace_to_bit_uint` would still produce the correct bit pattern.
    // We verify indirectly by checking that equality comparisons match a freshly
    // constructed big_int.
    using big_int_256 = beman::big_int::basic_big_int<256>;
    big_int_256 dst{0xFFFFFFFFFFFFFFFFU};
    dst = dst + big_int_256{1}; // promote to 2 limbs inline
    dst = big_int_256{7};       // shrink back to 1 limb inline -- tail must be zeroed
    EXPECT_EQ(dst, 7);
    EXPECT_EQ(dst, big_int_256{7});
    EXPECT_EQ(dst.representation().size(), 1U);
}

// ----- shrink_to_fit edge cases -----

TEST(Allocation, ShrinkToFitBackToInline) {
    beman::big_int::big_int x{42U};
    x.reserve(16);
    EXPECT_GE(x.capacity(), 16U);
    x.shrink_to_fit();
    // limb_count is 1 which fits in inplace_limbs=2, so should go back to inline
    EXPECT_EQ(x.capacity(), 0U);
    EXPECT_EQ(x.representation()[0], 42U);
}

TEST(Allocation, ShrinkToFitWhenCapacityEqualsCount) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    x.shrink_to_fit(); // goes back to inline
    x.shrink_to_fit(); // should be a no-op now
    EXPECT_EQ(x.representation()[0], 42U);
}

// ----- from_range with heap allocation -----

TEST(Allocation, FromRangeLargeAllocatesThenDestroys) {
#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
    std::array<beman::big_int::uint_multiprecision_t, 8> limbs{1, 2, 3, 4, 5, 6, 7, 8};
    beman::big_int::big_int                              x(std::from_range, limbs);
    EXPECT_EQ(x.representation().size(), 8U);
    EXPECT_EQ(x.representation()[0], 1U);
    EXPECT_EQ(x.representation()[7], 8U);
#endif
}

// ----- unary ops with heap storage -----

TEST(Allocation, NegateHeapAllocated) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    auto y = -x;
    EXPECT_EQ(y.representation()[0], 42U);
}

// ----- multiple grow/shrink cycles -----

TEST(Allocation, GrowShrinkGrowCycle) {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    EXPECT_GE(x.capacity(), 8U);
    x.shrink_to_fit();
    x.reserve(16);
    EXPECT_GE(x.capacity(), 16U);
    x.shrink_to_fit();
    EXPECT_EQ(x.representation()[0], 42U);
}

// ----- compile-time copy/move with heap -----

consteval bool test_copy_heap() {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    beman::big_int::big_int y(x);
    return y.representation()[0] == 42U;
}
static_assert(test_copy_heap());

consteval bool test_move_heap() {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    beman::big_int::big_int y(std::move(x));
    return y.representation()[0] == 42U;
}
static_assert(test_move_heap());

consteval bool test_shrink_to_fit_back_to_inline() {
    beman::big_int::big_int x{42U};
    x.reserve(16);
    x.shrink_to_fit();
    return x.capacity() == 0 && x.representation()[0] == 42U;
}
static_assert(test_shrink_to_fit_back_to_inline());

consteval bool test_grow_shrink_grow() {
    beman::big_int::big_int x{42U};
    x.reserve(8);
    x.shrink_to_fit();
    x.reserve(16);
    x.shrink_to_fit();
    return x.representation()[0] == 42U;
}
static_assert(test_grow_shrink_grow());
