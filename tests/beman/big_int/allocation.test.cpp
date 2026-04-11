// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/big_int.hpp>
#include <gtest/gtest.h>

// ----- compile-time tests -----

consteval bool test_size_default() {
    beman::big_int::big_int x;
    return x.size() == 1; // inplace_limbs for 64-bit = 1
}
static_assert(test_size_default());

consteval bool test_size_from_value() {
    beman::big_int::big_int x{42U};
    return x.size() == 1; // still inline, size() returns inplace_limbs
}
static_assert(test_size_from_value());

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

// ----- runtime tests -----

TEST(Allocation, SizeDefault) {
    beman::big_int::big_int x;
    EXPECT_EQ(x.size(), 1);
}

TEST(Allocation, SizeFromValue) {
    beman::big_int::big_int x{42U};
    EXPECT_EQ(x.size(), 1);
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
