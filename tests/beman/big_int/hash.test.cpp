// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <beman/big_int/big_int.hpp>

#include "testing.hpp"

namespace {

using beman::big_int::basic_big_int;
using beman::big_int::big_int;
using namespace beman::big_int::literals;

// ----- Type-level checks for the std::hash specialization -----

static_assert(std::is_default_constructible_v<std::hash<big_int>>);
static_assert(std::is_copy_constructible_v<std::hash<big_int>>);
static_assert(std::is_copy_assignable_v<std::hash<big_int>>);
static_assert(std::is_move_constructible_v<std::hash<big_int>>);
static_assert(std::is_move_assignable_v<std::hash<big_int>>);
static_assert(std::is_destructible_v<std::hash<big_int>>);
static_assert(std::is_swappable_v<std::hash<big_int>>);

static_assert(std::is_invocable_r_v<std::size_t, std::hash<big_int>, const big_int&>);
static_assert(std::is_nothrow_invocable_r_v<std::size_t, std::hash<big_int>, const big_int&>);

// The specialization must work for any inplace_bits / allocator parameterization,
// not just the convenience alias.
static_assert(std::is_default_constructible_v<std::hash<basic_big_int<32>>>);
static_assert(std::is_default_constructible_v<std::hash<basic_big_int<256>>>);
static_assert(std::is_default_constructible_v<std::hash<basic_big_int<1024>>>);
static_assert(std::is_nothrow_invocable_r_v<std::size_t, std::hash<basic_big_int<256>>, const basic_big_int<256>&>);

// ----- Determinism: hashing the same value twice yields the same hash -----

TEST(Hash, DeterminismSmall) {
    const std::hash<big_int> h{};
    const big_int            x{42};
    EXPECT_EQ(h(x), h(x));
}

TEST(Hash, DeterminismZero) {
    const std::hash<big_int> h{};
    const big_int            x{};
    EXPECT_EQ(h(x), h(x));
}

TEST(Hash, DeterminismMultiLimb) {
    const std::hash<big_int> h{};
    const big_int            x = 12345678901234567890123456789012345678901234567890_n;
    EXPECT_EQ(h(x), h(x));
}

TEST(Hash, DeterminismAcrossInstances) {
    // A freshly-constructed std::hash instance must agree with another instance.
    const std::hash<big_int> h1{};
    const std::hash<big_int> h2{};
    const big_int            x = 0xCAFE'BABE'DEAD'BEEF_n;
    EXPECT_EQ(h1(x), h2(x));
}

// ----- Equal values produce equal hashes (the only contract requirement) -----

TEST(Hash, EqualValuesHashEqually) {
    const std::hash<big_int> h{};
    const big_int            a{12345};
    const big_int            b{12345};
    ASSERT_EQ(a, b);
    EXPECT_EQ(h(a), h(b));
}

TEST(Hash, EqualValuesFromCopy) {
    const std::hash<big_int> h{};
    const big_int            a = 1'000'000'000'000'000'000_n;
    const big_int            b{a};
    ASSERT_EQ(a, b);
    EXPECT_EQ(h(a), h(b));
}

TEST(Hash, EqualValuesAfterMove) {
    const std::hash<big_int> h{};
    big_int                  a        = 0xDEAD'BEEF'CAFE'BABE'1234'5678'90AB'CDEF_n;
    const std::size_t        expected = h(a);
    const big_int            b{std::move(a)};
    EXPECT_EQ(h(b), expected);
}

TEST(Hash, EqualValuesAfterArithmetic) {
    const std::hash<big_int> h{};
    const big_int            a{1000};
    const big_int            b = big_int{700} + big_int{300};
    const big_int            c = big_int{2000} - big_int{1000};
    const big_int            d = big_int{500} * big_int{2};
    ASSERT_EQ(a, b);
    ASSERT_EQ(a, c);
    ASSERT_EQ(a, d);
    EXPECT_EQ(h(a), h(b));
    EXPECT_EQ(h(a), h(c));
    EXPECT_EQ(h(a), h(d));
}

TEST(Hash, EqualMultiLimbAfterArithmetic) {
    const std::hash<big_int> h{};
    const big_int            big        = 1_n << 200;
    const big_int            also_big_a = big + big_int{0};
    const big_int            also_big_b = (big_int{1} << 199) * big_int{2};
    ASSERT_EQ(big, also_big_a);
    ASSERT_EQ(big, also_big_b);
    EXPECT_EQ(h(big), h(also_big_a));
    EXPECT_EQ(h(big), h(also_big_b));
}

// ----- Zero is well-defined -----

TEST(Hash, ZeroIsConsistent) {
    const std::hash<big_int> h{};
    const big_int            z_default{};
    const big_int            z_from_int{0};
    const big_int            z_from_uint{0U};
    const big_int            z_from_subtraction = big_int{7} - big_int{7};

    ASSERT_TRUE(z_default == 0);
    ASSERT_EQ(z_default, z_from_int);
    ASSERT_EQ(z_default, z_from_uint);
    ASSERT_EQ(z_default, z_from_subtraction);
    EXPECT_EQ(h(z_default), h(z_from_int));
    EXPECT_EQ(h(z_default), h(z_from_uint));
    EXPECT_EQ(h(z_default), h(z_from_subtraction));
}

// ----- Different values produce different hashes (statistical / collision tests) -----

TEST(Hash, DistinctSmallValues) {
    const std::hash<big_int>        h{};
    std::unordered_set<std::size_t> hashes;
    for (int i = 0; i < 8; ++i) {
        hashes.insert(h(big_int{i}));
    }
    // For a good keyed hash and only 8 distinct inputs, the chance of any
    // collision is astronomically small. Require all eight to be distinct.
    EXPECT_EQ(hashes.size(), 8U);
}

TEST(Hash, DistinctValuesFromOneToTwoFiftySix) {
    const std::hash<big_int>        h{};
    std::unordered_set<std::size_t> hashes;
    constexpr int                   N = 256;
    for (int i = 0; i < N; ++i) {
        hashes.insert(h(big_int{i}));
    }
    // We allow a small slack so this is not flaky on 32-bit `size_t` platforms,
    // where the hash is folded down to 32 bits and the birthday bound predicts
    // a non-zero (but still small) collision probability over 256 inputs.
    EXPECT_GE(hashes.size(), static_cast<std::size_t>(N - 4));
}

TEST(Hash, DistinctMultiLimbValues) {
    const std::hash<big_int> h{};
    const big_int            base = big_int{1} << 200;
    const big_int            a    = base + big_int{1};
    const big_int            b    = base + big_int{2};
    const big_int            c    = (big_int{1} << 201) + big_int{1};
    EXPECT_NE(h(a), h(b));
    EXPECT_NE(h(a), h(c));
    EXPECT_NE(h(b), h(c));
}

TEST(Hash, DistinctPowersOfTwo) {
    const std::hash<big_int> h{};
    const big_int            a = big_int{1} << 100;
    const big_int            b = big_int{1} << 200;
    const big_int            c = big_int{1} << 300;
    EXPECT_NE(h(a), h(b));
    EXPECT_NE(h(b), h(c));
    EXPECT_NE(h(a), h(c));
}

TEST(Hash, SingleVsMultiLimbDistinct) {
    // Make sure a value that fits in one limb and a multi-limb value differ.
    const std::hash<big_int> h{};
    const big_int            small{1};
    const big_int            wide = big_int{1} << 128;
    EXPECT_NE(h(small), h(wide));
}

// ----- Stability across different inplace_bits parameterizations -----

TEST(Hash, CrossInplaceBitsForSmallValue) {
    // The hash is computed over `representation()`, which is a span of
    // `uint_multiprecision_t` limbs and is independent of `inplace_bits`.
    // The hash must therefore agree across all parameterizations.
    const basic_big_int<32>   a{42};
    const basic_big_int<64>   b{42};
    const basic_big_int<256>  c{42};
    const basic_big_int<1024> d{42};

    const auto ha = std::hash<basic_big_int<32>>{}(a);
    const auto hb = std::hash<basic_big_int<64>>{}(b);
    const auto hc = std::hash<basic_big_int<256>>{}(c);
    const auto hd = std::hash<basic_big_int<1024>>{}(d);

    EXPECT_EQ(ha, hb);
    EXPECT_EQ(ha, hc);
    EXPECT_EQ(ha, hd);
}

TEST(Hash, CrossInplaceBitsForMultiLimb) {
    const basic_big_int<64>   a = 1'000'000'000'000'000'000'000'000'000_n;
    const basic_big_int<2048> b{a};

    EXPECT_EQ(std::hash<basic_big_int<64>>{}(a), std::hash<basic_big_int<2048>>{}(b));
}

TEST(Hash, CrossInplaceBitsForZero) {
    const basic_big_int<32>   a{};
    const basic_big_int<2048> b{};

    EXPECT_EQ(std::hash<basic_big_int<32>>{}(a), std::hash<basic_big_int<2048>>{}(b));
}

// ----- Usability in standard unordered associative containers -----

TEST(Hash, UnorderedSetSmallKeys) {
    std::unordered_set<big_int> s;
    s.insert(big_int{1});
    s.insert(big_int{2});
    s.insert(big_int{3});
    s.insert(big_int{2}); // duplicate
    s.insert(big_int{1}); // duplicate
    EXPECT_EQ(s.size(), 3U);
    EXPECT_TRUE(s.contains(big_int{1}));
    EXPECT_TRUE(s.contains(big_int{2}));
    EXPECT_TRUE(s.contains(big_int{3}));
    EXPECT_FALSE(s.contains(big_int{0}));
    EXPECT_FALSE(s.contains(big_int{4}));
}

TEST(Hash, UnorderedSetMultiLimbKeys) {
    std::unordered_set<big_int> s;
    const big_int               k1 = 100'000'000'000'000'000'000'000_n;
    const big_int               k2 = 200'000'000'000'000'000'000'000_n;
    const big_int               k3 = (big_int{1} << 256) + big_int{1};
    s.insert(k1);
    s.insert(k2);
    s.insert(k3);
    s.insert(k1); // duplicate
    EXPECT_EQ(s.size(), 3U);
    EXPECT_TRUE(s.contains(k1));
    EXPECT_TRUE(s.contains(k2));
    EXPECT_TRUE(s.contains(k3));
    EXPECT_FALSE(s.contains(big_int{0}));
}

TEST(Hash, UnorderedSetWithBothSignsAreDistinctElements) {
    // Both the hash and operator== must distinguish +x from -x.
    std::unordered_set<big_int> s;
    s.insert(big_int{5});
    s.insert(big_int{-5});
    s.insert(big_int{5}); // duplicate
    EXPECT_EQ(s.size(), 2U);
    EXPECT_TRUE(s.contains(big_int{5}));
    EXPECT_TRUE(s.contains(big_int{-5}));
}

TEST(Hash, SignDistinguishesSmallValues) {
    const std::hash<big_int> h{};
    for (int i = 1; i <= 32; ++i) {
        const big_int pos{i};
        const big_int neg{-i};
        EXPECT_NE(h(pos), h(neg)) << "collision at i=" << i;
    }
}

TEST(Hash, SignDistinguishesMultiLimbValues) {
    const std::hash<big_int> h{};
    const big_int            magnitude = (big_int{1} << 200) + big_int{12345};
    const big_int            negated   = -magnitude;
    ASSERT_NE(magnitude, negated);
    EXPECT_NE(h(magnitude), h(negated));
}

TEST(Hash, SignDistinguishesAcrossInplaceBits) {
    // The sign-aware hash must agree across inplace_bits parameterizations,
    // both for positive and for negative values.
    const basic_big_int<64>  pos_64{42};
    const basic_big_int<256> pos_256{42};
    const basic_big_int<64>  neg_64  = -pos_64;
    const basic_big_int<256> neg_256 = -pos_256;

    EXPECT_EQ(std::hash<basic_big_int<64>>{}(pos_64), std::hash<basic_big_int<256>>{}(pos_256));
    EXPECT_EQ(std::hash<basic_big_int<64>>{}(neg_64), std::hash<basic_big_int<256>>{}(neg_256));
    EXPECT_NE(std::hash<basic_big_int<64>>{}(pos_64), std::hash<basic_big_int<64>>{}(neg_64));
}

TEST(Hash, NegatingTwiceRestoresHash) {
    const std::hash<big_int> h{};
    const big_int            x = (big_int{1} << 100) + big_int{7};
    const std::size_t        original = h(x);
    const big_int            negated = -x;
    EXPECT_NE(original, h(negated));
    EXPECT_EQ(original, h(-negated));
}

TEST(Hash, UnorderedMapBigIntKey) {
    std::unordered_map<big_int, int> m;
    const big_int                    k1 = 10'000'000'000'000'000'000'000_n;
    const big_int                    k2 = 20'000'000'000'000'000'000'000_n;
    m[k1]                               = 1;
    m[k2]                               = 2;
    m[k1]                               = 11; // overwrite
    EXPECT_EQ(m.size(), 2U);
    EXPECT_EQ(m.at(k1), 11);
    EXPECT_EQ(m.at(k2), 2);
    EXPECT_EQ(m.count(big_int{0}), 0U);
}

TEST(Hash, UnorderedMapManyEntries) {
    std::unordered_map<big_int, int> m;
    constexpr int                    N = 100;
    for (int i = 0; i < N; ++i) {
        m.emplace(big_int{i} * big_int{1'000'000'000'000'000'000} + big_int{i}, i);
    }
    EXPECT_EQ(m.size(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        const big_int k = big_int{i} * big_int{1'000'000'000'000'000'000} + big_int{i};
        EXPECT_EQ(m.at(k), i);
    }
}

// ----- Boundary values -----

TEST(Hash, BoundaryUint64Values) {
    const std::hash<big_int> h{};
    const big_int            u64_max{std::numeric_limits<std::uint64_t>::max()};
    const big_int            u64_max_minus_one{std::numeric_limits<std::uint64_t>::max() - 1U};
    const big_int            u64_max_plus_one = big_int{std::numeric_limits<std::uint64_t>::max()} + big_int{1};

    EXPECT_NE(h(u64_max), h(u64_max_minus_one));
    EXPECT_NE(h(u64_max), h(u64_max_plus_one));
    EXPECT_NE(h(u64_max_minus_one), h(u64_max_plus_one));
}

TEST(Hash, BoundarySignedInt64Min) {
    const std::hash<big_int> h{};
    const big_int            i64_min{std::numeric_limits<std::int64_t>::min()};
    const big_int            i64_min_copy{i64_min};
    EXPECT_EQ(h(i64_min), h(i64_min_copy));
}

} // namespace
