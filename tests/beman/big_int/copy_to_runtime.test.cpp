// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>
#include <gtest/gtest.h>

#include "testing.hpp"

using namespace beman::big_int::literals;

// ----- compile-time tests -----

consteval bool test_small_value() {
    constexpr auto v = beman::big_int::copy_to_runtime<decltype([]() { return beman::big_int::big_int{42}; })>();
    static_assert(is_inplace(v));
    return v.width_mag() == 6 && v.representation()[0] == 42U;
}
static_assert(test_small_value());

consteval bool test_zero_value() {
    constexpr auto v = beman::big_int::copy_to_runtime<decltype([]() { return beman::big_int::big_int{0}; })>();
    static_assert(is_inplace(v));
    return v.width_mag() == 0 && v.representation().size() == 1;
}
static_assert(test_zero_value());

consteval bool test_negative_value() {
    constexpr auto v = beman::big_int::copy_to_runtime<decltype([]() { return beman::big_int::big_int{-42}; })>();
    static_assert(is_inplace(v));
    return v.width_mag() == 6 && v.representation()[0] == 42U;
}
static_assert(test_negative_value());

consteval bool test_heap_required_value() {
    // 10^40 * 10^40 = 10^80 needs ~266 bits, so the source big_int must
    // allocate on the heap during consteval evaluation.
    // The result must hold all those limbs inline.
    constexpr auto v = beman::big_int::copy_to_runtime<decltype([]() {
        return 1'000'000'000'000'000'000'000'000'000'000'000'000'000_n *
               1'000'000'000'000'000'000'000'000'000'000'000'000'000_n;
    })>();
    static_assert(is_inplace(v));
    static_assert(decltype(v)::inplace_capacity >= 4);
    return v.representation().size() >= 4;
}
static_assert(test_heap_required_value());

consteval bool test_macro_form() {
    constexpr auto v = BEMAN_BIG_INT_COPY_TO_RUNTIME(beman::big_int::big_int{42});
    return v.representation()[0] == 42U;
}
static_assert(test_macro_form());

consteval bool test_allocator_preserved() {
    using custom_big_int = beman::big_int::basic_big_int<32,
                                                         beman::big_int::uint_multiprecision_t,
                                                         std::allocator<beman::big_int::uint_multiprecision_t>>;
    constexpr auto v     = beman::big_int::copy_to_runtime<decltype([]() { return custom_big_int{42}; })>();
    static_assert(std::is_same_v<typename decltype(v)::allocator_type, custom_big_int::allocator_type>);
    return v.representation()[0] == 42U;
}
static_assert(test_allocator_preserved());

// ----- runtime tests -----

TEST(CopyToRuntime, BridgesHeapValueToRuntime) {
    constexpr auto kVal = beman::big_int::copy_to_runtime<decltype([]() {
        return 1'000'000'000'000'000'000'000'000'000'000'000'000'000_n;
    })>();

    auto runtime_copy = kVal;
    EXPECT_EQ(runtime_copy.width_mag(), kVal.width_mag());

    beman::big_int::big_int as_default{kVal};
    EXPECT_EQ(as_default, 1'000'000'000'000'000'000'000'000'000'000'000'000'000_n);
}

TEST(CopyToRuntime, NegativeRoundTrip) {
    constexpr auto kVal =
        beman::big_int::copy_to_runtime<decltype([]() { return beman::big_int::big_int{-1} * (1_n << 200); })>();
    beman::big_int::big_int as_default{kVal};
    EXPECT_LT(as_default, beman::big_int::big_int{0});
    EXPECT_EQ(-as_default, beman::big_int::big_int{1} << 200);
}

TEST(CopyToRuntime, ZeroIsCanonical) {
    constexpr auto kZero = beman::big_int::copy_to_runtime<decltype([]() { return beman::big_int::big_int{0}; })>();
    EXPECT_EQ(kZero.width_mag(), 0U);
    beman::big_int::big_int as_default{kZero};
    EXPECT_EQ(as_default, beman::big_int::big_int{0});
}

TEST(CopyToRuntime, ConstexprCanBeUsedForComparison) {
    constexpr auto          kThreshold = BEMAN_BIG_INT_COPY_TO_RUNTIME(1_n << 100);
    beman::big_int::big_int input      = (beman::big_int::big_int{1} << 99) + beman::big_int::big_int{1};
    EXPECT_LT(input, beman::big_int::big_int{kThreshold});
}

TEST(CopyToRuntime, MacroExpression) {
    constexpr auto kVal = BEMAN_BIG_INT_COPY_TO_RUNTIME(1'000'000'000'000'000'000'000_n * 1'000'000'000'000'000'000_n);
    beman::big_int::big_int as_default{kVal};
    EXPECT_EQ(as_default, 1'000'000'000'000'000'000'000_n * 1'000'000'000'000'000'000_n);
}

TEST(CopyToRuntime, ResultLimbCountMatchesValue) {
    constexpr auto kVal = beman::big_int::copy_to_runtime<decltype([]() { return 1_n << 200; })>();
    EXPECT_EQ(kVal.representation().size(), decltype(kVal)::inplace_capacity);
    EXPECT_GE(decltype(kVal)::inplace_capacity, 4U);
}
