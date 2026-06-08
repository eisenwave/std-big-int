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

// FFT (small-prime NTT) multiplication. Operands are split into base-2^b
// coefficients (b chosen per multiply by fft_choose_coeff_bits), convolved modulo
// two of the NTT primes, recombined per coefficient with a two-prime Garner CRT,
// and carry-propagated into the product. Everything runs in std::uint64_t; the
// (dispatcher-supplied) uint64 workspace decouples it from the library limb
// width. b need not divide the limb width, so packing and recomposition use
// sliding bit buffers.

namespace beman::big_int::detail {

namespace {

// Width of the std::uint64_t words the bit buffers and accumulator are built from.
inline constexpr unsigned word_bits = 64;

// Split `src` (limb_bits-wide limbs) into base-2^b coefficients in `coeffs`, then
// zero-fill the remainder (the transform padding). Uses a 128-bit sliding bit
// buffer because b need not divide the limb width.
void fft_pack(const std::span<std::uint64_t>               coeffs,
              const std::span<const uint_multiprecision_t> src,
              const unsigned                               b) noexcept {
    constexpr unsigned  limb_bits = static_cast<unsigned>(width_v<uint_multiprecision_t>);
    const std::uint64_t mask      = (std::uint64_t{1} << b) - 1;
    std::uint64_t       buf_lo    = 0;
    std::uint64_t       buf_hi    = 0;
    unsigned            nbits     = 0; // valid bits in the buffer (always < b before a limb is added)
    std::size_t         out       = 0;
    for (const uint_multiprecision_t limb : src) {
        const std::uint64_t value = static_cast<std::uint64_t>(limb);
        if (nbits == 0) {
            buf_lo = value;
        } else {
            buf_lo |= value << nbits;
            buf_hi |= value >> (word_bits - nbits);
        }
        nbits += limb_bits;
        while (nbits >= b) {
            coeffs[out++] = buf_lo & mask;
            buf_lo        = (buf_lo >> b) | (buf_hi << (word_bits - b));
            buf_hi >>= b;
            nbits -= b;
        }
    }
    if (nbits != 0) {
        coeffs[out++] = buf_lo & mask;
    }
    for (; out < coeffs.size(); ++out) {
        coeffs[out] = 0;
    }
}

// Two-prime Garner CRT: returns the unique value in [0, p0*p1) (~124 bits, two
// limbs) congruent to r0 mod p0 and r1 mod p1. `inv01` is (p0 mod p1)^-1 mod p1.
[[nodiscard]] wide<std::uint64_t>
crt2(const std::uint64_t r0, const std::uint64_t r1, const std::uint64_t inv01) noexcept {
    const ntt_modulus& m0 = ntt_primes[0];
    const ntt_modulus& m1 = ntt_primes[1];
    // r0 < p0 < p1, so r0 is a valid residue mod p1.
    const std::uint64_t       c1   = m1.mul(m1.sub(r1, r0), inv01);
    const wide<std::uint64_t> prod = widening_mul(m0.p, c1); // p0 * c1 < p0*p1
    const auto                low  = carrying_add(prod.low_bits, r0);
    return {low.value, prod.high_bits + std::uint64_t{low.carry}};
}

// CRT-combine the two primes' residues coefficient by coefficient and
// carry-propagate result = sum_k c[k] * 2^(b*k) into `result`. A 3-word (192-bit)
// accumulator holds the active window of the product starting at the next output
// limb; each ~124-bit CRT coefficient is added at its bit offset within the
// window, and whole limbs are flushed as the window advances. `result.size()`
// must be exactly the product length so the accumulator drains to zero.
void fft_recompose(const std::span<uint_multiprecision_t> result,
                   const std::span<const std::uint64_t>   res0,
                   const std::span<const std::uint64_t>   res1,
                   const std::uint64_t                    inv01,
                   const unsigned                         b) noexcept {
    constexpr unsigned limb_bits    = static_cast<unsigned>(width_v<uint_multiprecision_t>);
    const std::size_t  result_coeff = res0.size();
    const std::size_t  result_limbs = result.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(res1.size() == result_coeff);

    std::uint64_t acc0 = 0; // 192-bit accumulator, low word first, holding the
    std::uint64_t acc1 = 0; // product window that starts at bit out * limb_bits
    std::uint64_t acc2 = 0;
    std::size_t   out  = 0;

    const auto flush_limb = [&]() noexcept {
        result[out++] = static_cast<uint_multiprecision_t>(acc0);
        if constexpr (limb_bits == word_bits) {
            acc0 = acc1;
            acc1 = acc2;
            acc2 = 0;
        } else {
            acc0 = (acc0 >> limb_bits) | (acc1 << (word_bits - limb_bits));
            acc1 = (acc1 >> limb_bits) | (acc2 << (word_bits - limb_bits));
            acc2 >>= limb_bits;
        }
    };

    for (std::size_t k = 0; k < result_coeff; ++k) {
        std::size_t offset = b * k - out * limb_bits; // bit offset of c[k] within the window
        while (offset >= limb_bits) {
            flush_limb();
            offset -= limb_bits;
        }
        const wide<std::uint64_t> c  = crt2(res0[k], res1[k], inv01);
        const unsigned            sh = static_cast<unsigned>(offset); // < limb_bits <= word_bits
        std::uint64_t             t0 = 0;
        std::uint64_t             t1 = 0;
        std::uint64_t             t2 = 0;
        if (sh == 0) {
            t0 = c.low_bits;
            t1 = c.high_bits;
        } else {
            t0 = c.low_bits << sh;
            t1 = (c.low_bits >> (word_bits - sh)) | (c.high_bits << sh);
            t2 = c.high_bits >> (word_bits - sh);
        }
        const auto s0 = carrying_add(acc0, t0);
        const auto s1 = carrying_add(acc1, t1, s0.carry);
        acc0          = s0.value;
        acc1          = s1.value;
        acc2          = acc2 + t2 + std::uint64_t{s1.carry};
    }

    while (out < result_limbs) {
        flush_limb();
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(acc0 == 0 && acc1 == 0 && acc2 == 0);
}

// (p0 mod p1)^-1 mod p1, the only CRT constant needed for two primes.
[[nodiscard]] std::uint64_t crt_inverse() noexcept { return ntt_primes[1].inv(ntt_primes[0].p % ntt_primes[1].p); }

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
    const unsigned    coeff_bits   = fft_choose_coeff_bits(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, coeff_bits) + fft_coeff_count(nb, coeff_bits) - 1;
    const std::size_t n            = fft_transform_length(na, nb);
    BEMAN_BIG_INT_DEBUG_ASSERT(workspace.size() >= fft_mul_storage_size(na, nb));
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::uint64_t>(std::countr_zero(n)) <= ntt_primes[1].log2_order);

    const auto ca   = workspace.subspan(0, n);
    const auto cb   = workspace.subspan(n, n);
    const auto tw   = workspace.subspan(2 * n, n / 2);
    const auto res0 = workspace.subspan(2 * n + n / 2, result_coeff);

    for (std::size_t t = 0; t < 2; ++t) {
        const ntt_modulus& m = ntt_primes[t];
        fft_pack(ca, a, coeff_bits);
        fft_pack(cb, b, coeff_bits);
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

    fft_recompose(result.first(na + nb), res0, ca.first(result_coeff), crt_inverse(), coeff_bits);
}

void square_fft(const std::span<uint_multiprecision_t>       result,
                const std::span<const uint_multiprecision_t> a_untrimmed,
                const std::span<std::uint64_t>               workspace) noexcept {
    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());

    const std::size_t na           = a.size();
    const unsigned    coeff_bits   = fft_choose_coeff_bits(na, na);
    const std::size_t result_coeff = 2 * fft_coeff_count(na, coeff_bits) - 1;
    const std::size_t n            = fft_transform_length(na, na);
    BEMAN_BIG_INT_DEBUG_ASSERT(workspace.size() >= square_fft_storage_size(na));
    BEMAN_BIG_INT_DEBUG_ASSERT(static_cast<std::uint64_t>(std::countr_zero(n)) <= ntt_primes[1].log2_order);

    const auto ca   = workspace.subspan(0, n);
    const auto tw   = workspace.subspan(n, n / 2);
    const auto save = workspace.subspan(n + n / 2, result_coeff);

    for (std::size_t t = 0; t < 2; ++t) {
        const ntt_modulus& m = ntt_primes[t];
        fft_pack(ca, a, coeff_bits);
        ntt_build_twiddles(tw, n, m, ntt_direction::forward);
        ntt_forward(ca, tw, m);
        ntt_pointwise(ca, ca, m);
        ntt_build_twiddles(tw, n, m, ntt_direction::inverse);
        ntt_inverse(ca, tw, m);
        if (t == 0) {
            std::copy_n(ca.begin(), result_coeff, save.begin());
        }
    }

    fft_recompose(result.first(2 * na), save, ca.first(result_coeff), crt_inverse(), coeff_bits);
}

} // namespace beman::big_int::detail
