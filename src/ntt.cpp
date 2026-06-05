// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/ntt.hpp>

#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mod_arith.hpp>

// Iterative radix-2 number-theoretic transform. Data is kept in ordinary
// residue form throughout; twiddles are carried in Montgomery form, so each
// butterfly multiply is a single mont_mul. Twiddles are generated on the fly by
// a running modular product (exact, so no drift), which keeps these routines
// allocation-free. Precomputed twiddle tables, radix-4, and lazy reduction are
// left as future optimizations.

namespace beman::big_int::detail {

void ntt_forward(const std::span<std::uint64_t> data, const ntt_modulus& mod) noexcept {
    const std::size_t n = data.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0 && (n & (n - 1)) == 0);
    BEMAN_BIG_INT_DEBUG_ASSERT((mod.p - 1) % static_cast<std::uint64_t>(n) == 0);

    const std::uint64_t mont_one = mod.to_mont(std::uint64_t{1});

    // Gentleman-Sande decimation-in-frequency: output ends up in bit-reversed order.
    for (std::size_t len = n; len > 1; len >>= 1) {
        const std::size_t   half      = len >> 1;
        const std::uint64_t wlen      = mod.pow(mod.g, (mod.p - 1) / static_cast<std::uint64_t>(len));
        const std::uint64_t wlen_mont = mod.to_mont(wlen);
        for (std::size_t start = 0; start < n; start += len) {
            std::uint64_t w = mont_one;
            for (std::size_t j = 0; j < half; ++j) {
                const std::uint64_t u  = data[start + j];
                const std::uint64_t v  = data[start + j + half];
                data[start + j]        = mod.add(u, v);
                data[start + j + half] = mod.mont_mul(mod.sub(u, v), w);
                w                      = mod.mont_mul(w, wlen_mont);
            }
        }
    }
}

void ntt_inverse(const std::span<std::uint64_t> data, const ntt_modulus& mod) noexcept {
    const std::size_t n = data.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(n != 0 && (n & (n - 1)) == 0);
    BEMAN_BIG_INT_DEBUG_ASSERT((mod.p - 1) % static_cast<std::uint64_t>(n) == 0);

    const std::uint64_t mont_one = mod.to_mont(std::uint64_t{1});

    // Cooley-Tukey decimation-in-time with inverse twiddles: consumes the
    // bit-reversed layout left by ntt_forward and restores natural order.
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t   half          = len >> 1;
        const std::uint64_t wlen          = mod.pow(mod.g, (mod.p - 1) / static_cast<std::uint64_t>(len));
        const std::uint64_t wlen_inv_mont = mod.to_mont(mod.inv(wlen));
        for (std::size_t start = 0; start < n; start += len) {
            std::uint64_t w = mont_one;
            for (std::size_t j = 0; j < half; ++j) {
                const std::uint64_t u  = data[start + j];
                const std::uint64_t v  = mod.mont_mul(data[start + j + half], w);
                data[start + j]        = mod.add(u, v);
                data[start + j + half] = mod.sub(u, v);
                w                      = mod.mont_mul(w, wlen_inv_mont);
            }
        }
    }

    // Scale by 1/n.
    const std::uint64_t n_inv_mont = mod.to_mont(mod.inv(static_cast<std::uint64_t>(n) % mod.p));
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = mod.mont_mul(data[i], n_inv_mont);
    }
}

void ntt_pointwise(const std::span<std::uint64_t>       a,
                   const std::span<const std::uint64_t> b,
                   const ntt_modulus&                   mod) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == b.size());
    const std::size_t n = a.size();
    for (std::size_t i = 0; i < n; ++i) {
        a[i] = mod.mul(a[i], b[i]);
    }
}

} // namespace beman::big_int::detail
