// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// The export regression net for `beman.big_int`.
//
// If a public entity in the headers loses its `BEMAN_BIG_INT_EXPORT` annotation (or,
// for a class member, the annotation on the enclosing class template), an importer
// can no longer name it, and this file must fail to compile. Most of the checks below
// are therefore static_assert / requires-expressions rather than TEST() bodies: a
// requires-expression that goes false, or a using-declaration that is a hard error,
// names the specific entity that lost its annotation instead of leaving a mystery
// "no member named X" wall for whoever regresses this.
//
// Some free functions (to_chars, from_chars, abs, div_rem_to_zero) are forward-declared
// with BEMAN_BIG_INT_EXPORT in big_int.hpp and only *defined* -- without repeating the
// annotation -- in charconv.hpp/numeric.hpp; that forward declaration is what a
// using-declaration below is really probing.

import beman.big_int;

#include <compare>
#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

// A using-declaration naming a non-exported (and not otherwise reachable) name is a
// hard compile error, so this block alone is a regression test for every free
// function's export annotation.
using beman::big_int::abs;
using beman::big_int::copy_to_runtime;
using beman::big_int::div_rem_to_zero;
using beman::big_int::from_chars;
using beman::big_int::gcd;
using beman::big_int::in_range;
using beman::big_int::lcm;
using beman::big_int::midpoint;
using beman::big_int::saturating_cast;
using beman::big_int::swap;
using beman::big_int::to_chars;
using beman::big_int::to_string;
using beman::big_int::to_wstring;

namespace {

using beman::big_int::basic_big_int;
using beman::big_int::big_int;
using beman::big_int::div_result;
using beman::big_int::uint_multiprecision_t;

// A non-default width, so the class template itself (and its default `Limb` /
// `Allocator` template arguments) is exercised, not just the `big_int` alias.
using wide_int = basic_big_int<512>;

// ----- Type identity -----

static_assert(
    std::is_same_v<big_int, basic_big_int<64, uint_multiprecision_t, std::allocator<uint_multiprecision_t>>>);
static_assert(std::is_same_v<beman::big_int::pmr::big_int, beman::big_int::pmr::basic_big_int<big_int::inplace_bits>>);
static_assert(
    std::is_same_v<beman::big_int::pmr::basic_big_int<256>,
                   basic_big_int<256, uint_multiprecision_t, std::pmr::polymorphic_allocator<uint_multiprecision_t>>>);
static_assert(std::is_same_v<wide_int::allocator_type, std::allocator<uint_multiprecision_t>>);

// ----- Member typedefs and constants (basic_big_int<64, ...>, i.e. big_int) -----

static_assert(std::is_same_v<big_int::allocator_type, std::allocator<uint_multiprecision_t>>);
static_assert(std::is_same_v<big_int::size_type, std::size_t>);
static_assert(std::is_same_v<big_int::pointer, std::allocator_traits<big_int::allocator_type>::pointer>);
static_assert(std::is_same_v<big_int::const_pointer, std::allocator_traits<big_int::allocator_type>::const_pointer>);
static_assert(big_int::inplace_bits > 0);
static_assert(big_int::inplace_capacity > 0);

// Same constants on a non-default width, so the check above is not accidentally only
// exercising values the alias happens to share with the primary template's defaults.
static_assert(wide_int::inplace_bits >= 512);
static_assert(wide_int::inplace_capacity > 0);

// ----- Member functions -----

static_assert(requires(const big_int& c, big_int& m, const big_int::size_type n) {
    { c.representation() } -> std::same_as<std::span<const uint_multiprecision_t>>;
    { c.representation_size() } -> std::same_as<big_int::size_type>;
    { c.representation_capacity() } -> std::same_as<big_int::size_type>;
    { c.size() } -> std::same_as<big_int::size_type>;
    { c.max_size() } -> std::same_as<big_int::size_type>;
    { c.capacity() } -> std::same_as<big_int::size_type>;
    { c.get_allocator() } -> std::same_as<big_int::allocator_type>;
    { m.reserve(n) } -> std::same_as<void>;
    { m.shrink_to_fit() } -> std::same_as<void>;
    { m.swap(m) } -> std::same_as<void>;
});

// The floating-point converting constructor is explicit; the integer one is not.
// (Verified against big_int.hpp: is_implicit_constructible_from is true only for a
// builtin integer or the same basic_big_int specialization, never for a float.)
static_assert(std::is_constructible_v<big_int, double> && !std::is_convertible_v<double, big_int>);
static_assert(std::is_constructible_v<big_int, int> && std::is_convertible_v<int, big_int>);

// ----- Operators -----
//
// Each requires-expression below goes false, rather than failing to compile the whole
// file, if the specific operator it names loses its BEMAN_BIG_INT_EXPORT (namespace-scope
// operators) or is dropped from the class body (member operators). Grouped by clause.

// [big.int.unary]
static_assert(requires(const big_int& a, big_int b) {
    { +a } -> std::same_as<big_int>;
    { -a } -> std::same_as<big_int>;
    { ~a } -> std::same_as<big_int>;
    { +std::move(b) } -> std::same_as<big_int>;
    { -std::move(b) } -> std::same_as<big_int>;
    { ~std::move(b) } -> std::same_as<big_int>;
});

// [big.int.modifiers], increment/decrement
static_assert(requires(big_int& m) {
    { ++m } -> std::same_as<big_int&>;
    { m++ } -> std::same_as<big_int>;
    { --m } -> std::same_as<big_int&>;
    { m-- } -> std::same_as<big_int>;
});

// [big.int.cmp]
static_assert(requires(const big_int& a, const big_int& b, const int i) {
    { a == b } -> std::same_as<bool>;
    { a <=> b } -> std::same_as<std::strong_ordering>;
    { a == i } -> std::same_as<bool>;
    { i == a } -> std::same_as<bool>;
    { a <=> i } -> std::same_as<std::strong_ordering>;
    { i <=> a } -> std::same_as<std::strong_ordering>;
});

// [big.int.binary]
static_assert(requires(const big_int& a, const big_int& b, const int i) {
    { a + b } -> std::same_as<big_int>;
    { a - b } -> std::same_as<big_int>;
    { a * b } -> std::same_as<big_int>;
    { a / b } -> std::same_as<big_int>;
    { a % b } -> std::same_as<big_int>;
    { a & b } -> std::same_as<big_int>;
    { a | b } -> std::same_as<big_int>;
    { a ^ b } -> std::same_as<big_int>;
    { a << 1 } -> std::same_as<big_int>;
    { a >> 1 } -> std::same_as<big_int>;
    // Mixed with a builtin integer on either side.
    { a + i } -> std::same_as<big_int>;
    { i + a } -> std::same_as<big_int>;
    { a - i } -> std::same_as<big_int>;
    { i - a } -> std::same_as<big_int>;
    { a * i } -> std::same_as<big_int>;
    { i * a } -> std::same_as<big_int>;
    { a / i } -> std::same_as<big_int>;
    { i / a } -> std::same_as<big_int>;
    { a % i } -> std::same_as<big_int>;
    { i % a } -> std::same_as<big_int>;
    { a & i } -> std::same_as<big_int>;
    { i & a } -> std::same_as<big_int>;
    { a | i } -> std::same_as<big_int>;
    { i | a } -> std::same_as<big_int>;
    { a ^ i } -> std::same_as<big_int>;
    { i ^ a } -> std::same_as<big_int>;
});

// Compound assignment: every form returns big_int&, on a mutable lvalue.
static_assert(requires(big_int& m, const big_int& a, const int i) {
    { m += a } -> std::same_as<big_int&>;
    { m -= a } -> std::same_as<big_int&>;
    { m *= a } -> std::same_as<big_int&>;
    { m /= a } -> std::same_as<big_int&>;
    { m %= a } -> std::same_as<big_int&>;
    { m &= a } -> std::same_as<big_int&>;
    { m |= a } -> std::same_as<big_int&>;
    { m ^= a } -> std::same_as<big_int&>;
    { m <<= 1 } -> std::same_as<big_int&>;
    { m >>= 1 } -> std::same_as<big_int&>;
    { m += i } -> std::same_as<big_int&>;
    { m -= i } -> std::same_as<big_int&>;
    { m *= i } -> std::same_as<big_int&>;
    { m /= i } -> std::same_as<big_int&>;
    { m %= i } -> std::same_as<big_int&>;
    { m &= i } -> std::same_as<big_int&>;
    { m |= i } -> std::same_as<big_int&>;
    { m ^= i } -> std::same_as<big_int&>;
});

// The explicit conversion to a builtin arithmetic type ([big.int.conv]).
static_assert(std::is_constructible_v<int, big_int> && !std::is_convertible_v<big_int, int>);
static_assert(std::is_constructible_v<double, big_int> && !std::is_convertible_v<big_int, double>);

// ----- div_result / div_rem_to_zero -----

static_assert(std::is_same_v<decltype(div_rem_to_zero(big_int{7}, big_int{2})), div_result<big_int>>);
static_assert(requires(const div_result<big_int>& d, const div_result<big_int>& e) {
    { d.quotient } -> std::same_as<const big_int&>;
    { d.remainder } -> std::same_as<const big_int&>;
    { d <=> e } -> std::same_as<std::strong_ordering>;
});

// ----- numeric_limits is deliberately NOT specialized: basic_big_int is unbounded -----

static_assert(!std::numeric_limits<big_int>::is_specialized);
static_assert(!std::numeric_limits<wide_int>::is_specialized);

// ----- copy_to_runtime -----
//
// Spelled exactly as an importer must: BEMAN_BIG_INT_COPY_TO_RUNTIME is a
// convenience macro in the separate copy_to_runtime.hpp header, and macros do not
// cross a module boundary, so it is deliberately unavailable here; the underlying
// consteval function template is called directly instead.
static_assert(static_cast<int>(copy_to_runtime<decltype([] { return big_int{42}; })>()) == 42);

} // namespace

// ----- A handful of TEST() cases, so ctest reports something beyond static_assert -----

TEST(Exports, Arithmetic) {
    const big_int a = 123;
    const big_int b = 45;
    EXPECT_EQ(a + b, big_int{168});
    EXPECT_EQ(a - b, big_int{78});
    EXPECT_EQ(a * b, big_int{5535});
    EXPECT_EQ(a / b, big_int{2});
    EXPECT_EQ(a % b, big_int{33});
    EXPECT_EQ(-a, big_int{-123});
}

TEST(Exports, NumericFunctions) {
    EXPECT_EQ(abs(big_int{-7}), big_int{7});
    EXPECT_EQ(abs(big_int{7}), big_int{7});
    EXPECT_EQ(gcd(big_int{54}, big_int{24}), big_int{6});
    EXPECT_EQ(lcm(big_int{4}, big_int{6}), big_int{12});
    EXPECT_EQ(midpoint(big_int{4}, big_int{10}), big_int{7});
    EXPECT_EQ(saturating_cast<int>(big_int{1} << 100), std::numeric_limits<int>::max());
    EXPECT_TRUE(in_range<int>(big_int{42}));
    EXPECT_FALSE(in_range<int>(big_int{1} << 100));

    const auto qr = div_rem_to_zero(big_int{-7}, big_int{2});
    EXPECT_EQ(qr.quotient, big_int{-3});
    EXPECT_EQ(qr.remainder, big_int{-1});
}

TEST(Exports, TextConversions) {
    const big_int x = 255;
    EXPECT_EQ(to_string(x), "255");
    EXPECT_EQ(to_string(x, 16), "ff");
    EXPECT_EQ(to_wstring(x), L"255");

    char       buf[8]{};
    const auto to_res = to_chars(buf, buf + sizeof(buf), x, 16);
    EXPECT_EQ(to_res.ec, std::errc{});

    big_int    parsed;
    const auto from_res = from_chars(buf, to_res.ptr, parsed, 16);
    EXPECT_EQ(from_res.ec, std::errc{});
    EXPECT_EQ(parsed, x);
}

TEST(Exports, LiteralsAndWideWidth) {
    using namespace beman::big_int::literals;

    const big_int a = 12'345'678'901'234'567'890_n;
    const big_int b = 12345678901234567890_N;
    // Cross-width construction: `T` (here `big_int`) is a different basic_big_int
    // specialization than `wide_int`, so this converting constructor is explicit,
    // same as it is for a builtin floating-point source.
    const wide_int w(a);
    EXPECT_EQ(a, b);
    EXPECT_EQ(to_string(w), to_string(a));
}

TEST(Exports, SwapAndSize) {
    big_int a = 1;
    big_int b = 2;
    swap(a, b); // found by ADL / the using-declaration above, not std::swap
    EXPECT_EQ(a, big_int{2});
    EXPECT_EQ(b, big_int{1});

    big_int grown = 1;
    grown.reserve(4096);
    EXPECT_GE(grown.capacity(), std::size_t{4096}); // capacity() is a bit count, unlike representation_capacity()
    EXPECT_EQ(grown, big_int{1});
}
