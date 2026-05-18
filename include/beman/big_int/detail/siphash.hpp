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

struct state_holder {
    std::uint64_t v0;
    std::uint64_t v1;
    std::uint64_t v2;
    std::uint64_t v3;
};

namespace impl {

constexpr state_holder sipround(state_holder s) {
    s.v0 += s.v1;
    s.v1 = std::rotl(s.v1, 13);
    s.v1 ^= s.v0;
    s.v0 = std::rotl(s.v0, 32);

    s.v2 += s.v3;
    s.v3 = std::rotl(s.v3, 16);
    s.v3 ^= s.v2;

    s.v0 += s.v3;
    s.v3 = std::rotl(s.v3, 21);
    s.v3 ^= s.v0;

    s.v2 += s.v1;
    s.v1 = std::rotl(s.v1, 17);
    s.v1 ^= s.v2;
    s.v2 = std::rotl(s.v2, 32);
    return s;
}

constexpr state_holder compress(state_holder s, const std::uint64_t m) {
    s.v3 ^= m;
    s = sipround(s);
    s = sipround(s);
    s.v0 ^= m;
    return s;
}

} // namespace impl

constexpr std::size_t siphash(const std::span<const uint_multiprecision_t> limbs, const bool sign) {

    using impl::compress;
    using impl::sipround;

    constexpr std::uint64_t k0 = 0x0706050403020100ULL;
    constexpr std::uint64_t k1 = 0x0f0e0d0c0b0a0908ULL;
    state_holder            s{
        0x736f6d6570736575ULL ^ k0,
        0x646f72616e646f6dULL ^ k1,
        0x6c7967656e657261ULL ^ k0,
        0x7465646279746573ULL ^ k1,
    };

    // Fold the sign into the initial state so it propagates through every round
    if (sign) {
        s.v3 ^= std::numeric_limits<std::uint64_t>::max();
    }

    const auto byte_len = limbs.size() * sizeof(uint_multiprecision_t);

    if constexpr (sizeof(uint_multiprecision_t) == sizeof(std::uint64_t)) {
        for (const auto m : limbs) {
            s = compress(s, static_cast<std::uint64_t>(m));
        }
    } else {
        // Pack pairs of uint32_t into uint64_t words
        const auto pairs = limbs.size() / 2;
        for (std::size_t i = 0; i < pairs; ++i) {
            auto m = static_cast<std::uint64_t>(limbs[i * 2]) | (static_cast<std::uint64_t>(limbs[i * 2 + 1]) << 32);
            s      = compress(s, m);
        }
        // Odd trailing uint_multiprecision_t folds into the final block
    }

    // Final block: length in high byte + possible trailing uint32_t
    auto b = static_cast<std::uint64_t>(byte_len) << 56;
    if constexpr (sizeof(uint_multiprecision_t) == sizeof(std::uint32_t)) {
        if (limbs.size() & 1) {
            b |= static_cast<std::uint64_t>(limbs.back());
        }
    }

    s = compress(s, b);

    s.v2 ^= 0xFFULL;

    s = sipround(s);
    s = sipround(s);
    s = sipround(s);
    s = sipround(s);

    const auto h = s.v0 ^ s.v1 ^ s.v2 ^ s.v3;

    if constexpr (sizeof(std::size_t) == sizeof(std::uint64_t)) {
        return h;
    } else {
        BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
        BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wuseless-cast")
        return static_cast<std::size_t>(h ^ (h >> 32U));
        BEMAN_BIG_INT_DIAGNOSTIC_POP()
    }
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_SIPHASH_HPP
