// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_SIPHASH_HPP
#define BEMAN_BIG_INT_SIPHASH_HPP

#include <beman/big_int/detail/config.hpp>
#include <cstdint>
#include <cstddef>
#include <bit>
#include <span>

namespace beman::big_int::detail {

namespace impl {

constexpr void sipround(std::uint64_t& v0, std::uint64_t& v1, std::uint64_t& v2, std::uint64_t& v3) {
    v0 += v1;
    v1 = std::rotl(v1, 13);
    v1 ^= v0;
    v0 = std::rotl(v0, 32);

    v2 += v3;
    v3 = std::rotl(v3, 16);
    v3 ^= v2;

    v0 += v3;
    v3 = std::rotl(v3, 21);
    v3 ^= v0;

    v2 += v1;
    v1 = std::rotl(v1, 17);
    v1 ^= v2;
    v2 = std::rotl(v2, 32);
}

constexpr void compress(std::uint64_t& v0, std::uint64_t& v1, std::uint64_t& v2, std::uint64_t& v3, const std::uint64_t m) {
    v3 ^= m;

    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);

    v0 ^= m;
}

} // namespace impl

constexpr std::size_t siphash(const std::span<const uint_multiprecision_t> limbs) {

    using impl::sipround;
    using impl::compress;

    constexpr auto k0 = 0x0706050403020100ULL;
    constexpr auto k1 = 0x0f0e0d0c0b0a0908ULL;
    
    auto v0 = 0x736f6d6570736575ULL ^ k0;
    auto v1 = 0x646f72616e646f6dULL ^ k1;
    auto v2 = 0x6c7967656e657261ULL ^ k0;
    auto v3 = 0x7465646279746573ULL ^ k1;

    const auto byte_len = limbs.size() * sizeof(uint_multiprecision_t);

    if constexpr (std::same_as<uint_multiprecision_t, uint64_t>) {
        for (const auto m : limbs) {
            compress(v0, v1, v2, v3, m);
        }
    } else {
        // Pack pairs of uint32_t into uint64_t words
        const auto pairs = limbs.size() / 2;
        for (std::size_t i = 0; i < pairs; ++i) {
            auto m = limbs[i * 2] | (static_cast<uint64_t>(limbs[i * 2 + 1]) << 32);
            compress(v0, v1, v2, v3, m);
        }
        // Odd trailing uint_multiprecision_t folds into the final block
    }

    // Final block: length in high byte + possible trailing uint32_t
    auto b = static_cast<uint64_t>(byte_len) << 56;
    if constexpr (std::same_as<uint_multiprecision_t, uint32_t>) {
        if (limbs.size() & 1) {
            b |= limbs.back();
        }
    }

    compress(v0, v1, v2, v3, b);

    v2 ^= 0xFFULL;

    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);
    sipround(v0, v1, v2, v3);

    const auto h = v0 ^ v1 ^ v2 ^ v3;

    if constexpr (sizeof(std::size_t) == sizeof(std::uint64_t)) {
        return h;
    } else {
        return static_cast<std::size_t>(h ^ (h >> 32U));
    }
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_SIPHASH_HPP
