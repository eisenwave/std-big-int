// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/detail/mul_impl.hpp>
#include <beman/big_int/detail/ntt.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/mod_arith.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/wide_ops.hpp>

// FFT (small-prime NTT) multiplication. Operands are split into base-2^32
// coefficients, convolved modulo two of the NTT primes, recombined per
// coefficient with a two-prime Garner CRT, and carry-propagated into the
// product. Everything runs in std::uint64_t; the (dispatcher-supplied) uint64
// workspace decouples it from the library limb width, so the same code serves
// 64-bit limbs (two coefficients per limb) and 32-bit limbs (one).

namespace beman::big_int::detail {

namespace {

// Mask selecting one packed coefficient (the low fft_coeff_bits of a value).
inline constexpr std::uint64_t coeff_mask = (std::uint64_t{1} << fft_coeff_bits) - 1;

// Bits in the std::uint64_t accumulator limb used by the recomposition.
inline constexpr std::size_t acc_limb_bits = 64;

// Split each limb of `src` into fft_coeffs_per_limb base-2^fft_coeff_bits
// coefficients in `coeffs`, then zero-fill the rest of `coeffs` (the padding).
void fft_pack(const std::span<std::uint64_t> coeffs, const std::span<const uint_multiprecision_t> src) noexcept {
    std::size_t idx = 0;
    for (const uint_multiprecision_t limb : src) {
        if constexpr (fft_coeffs_per_limb == 2) {
            coeffs[idx++] = static_cast<std::uint64_t>(limb) & coeff_mask;
            coeffs[idx++] = static_cast<std::uint64_t>(limb) >> fft_coeff_bits;
        } else {
            coeffs[idx++] = static_cast<std::uint64_t>(limb);
        }
    }
    for (; idx < coeffs.size(); ++idx) {
        coeffs[idx] = 0;
    }
}

// Two-prime Garner CRT: returns the unique value in [0, p0*p1) (~124 bits, two
// limbs) congruent to r0 mod p0 and r1 mod p1. `inv01` is (p0 mod p1)^-1 mod p1.
[[nodiscard]] wide<std::uint64_t> crt2(const std::uint64_t r0, const std::uint64_t r1, const std::uint64_t inv01) noexcept {
    const ntt_modulus& m0 = ntt_primes[0];
    const ntt_modulus& m1 = ntt_primes[1];
    // r0 < p0 < p1, so r0 is a valid residue mod p1.
    const std::uint64_t       c1   = m1.mul(m1.sub(r1, r0), inv01);
    const wide<std::uint64_t> prod = widening_mul(m0.p, c1); // p0 * c1 < p0*p1
    const auto                low  = carrying_add(prod.low_bits, r0);
    return {low.value, prod.high_bits + std::uint64_t{low.carry}};
}

// CRT-combine the two primes' residues coefficient by coefficient and
// carry-propagate result = sum_k c[k] * 2^(fft_coeff_bits*k) into `result`.
// Streams fft_coeff_bits-wide digits with a 128-bit accumulator (kept < 2^94 by
// the per-digit >> fft_coeff_bits), which also makes the layout limb-width
// agnostic. `result.size()` must be exactly the product length so the
// accumulator drains to zero.
void fft_recompose(const std::span<uint_multiprecision_t> result,
                   const std::span<const std::uint64_t>   res0,
                   const std::span<const std::uint64_t>   res1,
                   const std::uint64_t                    inv01) noexcept {
    const std::size_t result_coeff = res0.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(res1.size() == result_coeff);
    const std::size_t num_digits = result.size() * fft_coeffs_per_limb;

    std::uint64_t acc_lo = 0;
    std::uint64_t acc_hi = 0;
    for (std::size_t k = 0; k < num_digits; ++k) {
        if (k < result_coeff) {
            const wide<std::uint64_t> c   = crt2(res0[k], res1[k], inv01);
            const auto                low = carrying_add(acc_lo, c.low_bits);
            acc_lo                        = low.value;
            acc_hi                        = acc_hi + c.high_bits + std::uint64_t{low.carry};
        }
        const std::uint64_t digit = acc_lo & coeff_mask;
        acc_lo                    = (acc_lo >> fft_coeff_bits) | (acc_hi << (acc_limb_bits - fft_coeff_bits));
        acc_hi                    = acc_hi >> fft_coeff_bits;

        const std::size_t limb = k / fft_coeffs_per_limb;
        const unsigned    sub  = static_cast<unsigned>((k % fft_coeffs_per_limb) * fft_coeff_bits);
        if (sub == 0) {
            result[limb] = static_cast<uint_multiprecision_t>(digit);
        } else {
            result[limb] |= static_cast<uint_multiprecision_t>(digit) << sub;
        }
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(acc_lo == 0 && acc_hi == 0);
}

// (p0 mod p1)^-1 mod p1, the only CRT constant needed for two primes.
[[nodiscard]] std::uint64_t crt_inverse() noexcept {
    return ntt_primes[1].inv(ntt_primes[0].p % ntt_primes[1].p);
}

} // namespace

void multiply_fft(const std::span<uint_multiprecision_t>       result,
                  const std::span<const uint_multiprecision_t> a_untrimmed,
                  const std::span<const uint_multiprecision_t> b_untrimmed,
                  const std::span<std::uint64_t>               workspace) noexcept {
    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty() && !b.empty());

    const std::size_t na           = a.size();
    const std::size_t nb           = b.size();
    const std::size_t result_coeff = fft_coeffs_per_limb * (na + nb) - 1;
    const std::size_t n            = fft_transform_length(na, nb);
    BEMAN_BIG_INT_DEBUG_ASSERT(workspace.size() >= fft_mul_storage_size(na, nb));
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::uint64_t>(std::countr_zero(n)) <= ntt_primes[1].log2_order);

    const auto ca   = workspace.subspan(0, n);
    const auto cb   = workspace.subspan(n, n);
    const auto tw   = workspace.subspan(2 * n, n / 2);
    const auto res0 = workspace.subspan(2 * n + n / 2, result_coeff);

    for (std::size_t t = 0; t < 2; ++t) {
        const ntt_modulus& m = ntt_primes[t];
        fft_pack(ca, a);
        fft_pack(cb, b);
        ntt_build_twiddles(tw, n, m, ntt_direction::forward);
        ntt_forward(ca, tw, m);
        ntt_forward(cb, tw, m);
        ntt_pointwise(ca, cb, m);
        ntt_build_twiddles(tw, n, m, ntt_direction::inverse);
        ntt_inverse(ca, tw, m);
        if (t == 0) {
            std::copy_n(ca.begin(), result_coeff, res0.begin());
        }
    }

    fft_recompose(result.first(na + nb), res0, ca.first(result_coeff), crt_inverse());
}

void square_fft(const std::span<uint_multiprecision_t>       result,
                const std::span<const uint_multiprecision_t> a_untrimmed,
                const std::span<std::uint64_t>               workspace) noexcept {
    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());

    const std::size_t na           = a.size();
    const std::size_t result_coeff = 2 * fft_coeffs_per_limb * na - 1;
    const std::size_t n            = fft_transform_length(na, na);
    BEMAN_BIG_INT_DEBUG_ASSERT(workspace.size() >= square_fft_storage_size(na));
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::uint64_t>(std::countr_zero(n)) <= ntt_primes[1].log2_order);

    const auto ca   = workspace.subspan(0, n);
    const auto tw   = workspace.subspan(n, n / 2);
    const auto save = workspace.subspan(n + n / 2, result_coeff);

    for (std::size_t t = 0; t < 2; ++t) {
        const ntt_modulus& m = ntt_primes[t];
        fft_pack(ca, a);
        ntt_build_twiddles(tw, n, m, ntt_direction::forward);
        ntt_forward(ca, tw, m);
        ntt_pointwise(ca, ca, m);
        ntt_build_twiddles(tw, n, m, ntt_direction::inverse);
        ntt_inverse(ca, tw, m);
        if (t == 0) {
            std::copy_n(ca.begin(), result_coeff, save.begin());
        }
    }

    fft_recompose(result.first(2 * na), save, ca.first(result_coeff), crt_inverse());
}

} // namespace beman::big_int::detail
