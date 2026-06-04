// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MUL_IMPL_HPP
#define BEMAN_BIG_INT_MUL_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>

#include <algorithm>
#include <bit>
#include <compare>
#include <memory>
#include <span>

namespace beman::big_int::detail {

// ---------------------------------------------------------------------------
// Long (classical) O(n*m) multiplication. Writes exactly `a.size() + b.size()`
// limbs into `result.first(a.size() + b.size())`; limbs beyond that are
// untouched. `result` need NOT be pre-zeroed.
// `result` must NOT alias `a` or `b`. Both `a` and `b` must be non-empty.
// ---------------------------------------------------------------------------
constexpr void multiply_long(const std::span<uint_multiprecision_t>       result,
                             const std::span<const uint_multiprecision_t> a,
                             const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + b.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());

    // The key invariant from Boost is:
    //   double_limb_max - 2 * limb_max >= limb_max * limb_max
    // This means that: widening_mul(a[i], b[j]).high + carry + bool_carry
    // can never overflow a single limb, so we only need a single-limb carry.

    // First row (i=0): write directly into result without reading. This avoids
    // the pre-zero precondition that the accumulating path below would need.
    {
        uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const auto [lo, hi] = widening_mul(a[0], b[j]);
            const auto [s, c]   = carrying_add(lo, carry);
            result[j]           = s;
            carry               = hi + static_cast<uint_multiprecision_t>(c);
        }
        result[b.size()] = carry;
    }

    // Subsequent rows: accumulate onto values written by previous rows.
    for (std::size_t i = 1; i < a.size(); ++i) {
        uint_multiprecision_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const auto [lo, hi] = widening_mul(a[i], b[j]);
            const auto [s1, c1] = carrying_add(lo, result[i + j]);
            const auto [s2, c2] = carrying_add(s1, carry);
            result[i + j]       = s2;
            carry               = hi + static_cast<uint_multiprecision_t>(c1) + static_cast<uint_multiprecision_t>(c2);
        }
        result[i + b.size()] = carry;
    }
}

// ---------------------------------------------------------------------------
// Schoolbook squaring: result <- a * a (HAC algorithm 14.16). Computes the
// off-diagonal upper triangle once (n*(n-1)/2 widening muls), doubles it with
// a one-bit shift, then folds in the n diagonal squares: roughly half the
// widening muls of multiply_long. Unlike multiply_long, `result` MUST be
// pre-zeroed and have space for 2 * a.size() limbs. `result` must NOT alias `a`.
// ---------------------------------------------------------------------------
constexpr void square_long(const std::span<uint_multiprecision_t>       result,
                           const std::span<const uint_multiprecision_t> a) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const std::size_t n = a.size();

    // Off-diagonal upper triangle: result += sum_{i<j} a[i]*a[j] * B^(i+j).
    for (std::size_t i = 0; i + 1 < n; ++i) {
        uint_multiprecision_t carry = 0;
        for (std::size_t j = i + 1; j < n; ++j) {
            const auto [lo, hi] = widening_mul(a[i], a[j]);
            const auto [s1, c1] = carrying_add(lo, result[i + j]);
            const auto [s2, c2] = carrying_add(s1, carry);
            result[i + j]       = s2;
            carry               = hi + static_cast<uint_multiprecision_t>(c1) + static_cast<uint_multiprecision_t>(c2);
        }
        result[i + n] = carry;
    }

    // Double the triangle; cannot overflow 2n limbs since a*a < B^(2n).
    [[maybe_unused]] const auto doubled_size = shift_left_n(result.first(2 * n), 2 * n, 1u);
    BEMAN_BIG_INT_DEBUG_ASSERT(doubled_size == 2 * n);

    // Diagonal: result += sum a[i]^2 * B^(2i). The carry out of slot 2i+1
    // lands in slot 2i+2, the next iteration's low slot, so one flag suffices.
    bool carry_flag = false;
    for (std::size_t i = 0; i < n; ++i) {
        const auto [lo, hi] = widening_mul(a[i], a[i]);
        const auto [s1, c1] = carrying_add(result[2 * i], lo, carry_flag);
        result[2 * i]       = s1;
        const auto [s2, c2] = carrying_add(result[2 * i + 1], hi, c1);
        result[2 * i + 1]   = s2;
        carry_flag          = c2;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(!carry_flag);
}

// Below this limb count square_dispatch routes squares back to plain
// schoolbook: the three-pass structure of square_long loses there (OpenJDK's
// MULTIPLY_SQUARE_THRESHOLD draws the same line at 640 bits).
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t square_long_cutoff = 8;

// Minimum number of limbs for Karatsuba to be worthwhile
// Directly from Boost, and reconfirmed as correct
inline constexpr std::size_t karatsuba_cutoff   = 48;
inline constexpr std::size_t karatsuba_fallback = 24;

// Heuristic estimate of scratch space needed for Karatsuba multiplication.
// One Karatsuba level uses ~2*s limbs (t1=2n+2, t2=t3=n+1 with n=s/2+1). The
// geometric sum over self-recursion converges to 4*s as the asymptotic worst
// case; empirically (probed via scratch_allocator high-water marks on sizes
// 40-4000 limbs in scratch_peak_bench) the actual peak/s ratio tops out at
// ~3.997. 5*s leaves ~25% safety margin and matches the same generous-but-not-
// wasteful ratio used by the Toom-Cook variants.
constexpr std::size_t karatsuba_storage_size(const std::size_t s) noexcept { return 5 * s; }

// ---------------------------------------------------------------------------
// Recursive Karatsuba multiplication.
// Port of Boost.Multiprecision multiply_karatsuba (lines 98-215).
//
// `result` must have space for a.size() + b.size() limbs or more.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// ---------------------------------------------------------------------------
void multiply_karatsuba(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        const std::span<const uint_multiprecision_t> b_untrimmed,
                        scratch_allocator_base&                      scratch) noexcept;

// Minimum number of limbs for the Karatsuba squaring variant: the squaring
// basecase stays ahead of recursion roughly twice as long as schoolbook does
// against general Karatsuba (the same SQR/MUL threshold ratio GMP observes).
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t square_karatsuba_cutoff = 80;

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_karatsuba: one evaluation (a_h + a_l) per
// level instead of two, and all three sub-products are recursive squares.
// `result` must have space for 2 * a.size() limbs or more; slack beyond the
// product follows the same caller-pre-zeroed convention as the general kernel.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// ---------------------------------------------------------------------------
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it
// replaces `square_karatsuba_cutoff` for this call only; recursive
// sub-squares always use the default.
void square_karatsuba(const std::span<uint_multiprecision_t>       result,
                      const std::span<const uint_multiprecision_t> a_untrimmed,
                      scratch_allocator_base&                      scratch,
                      const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for Toom-Cook 3 to be worthwhile.
// See multiplication_stress_bench for tuning
inline constexpr std::size_t toom_cook_3_cutoff = 300;

// Heuristic estimate of scratch space needed for Toom-Cook 3 multiplication.
// One Toom-3 level uses 8k+10 limbs (~2.67*s where k = ceil(s/3)). The
// geometric sum over self-recursion converges to (8/3)*(3/2)*s = 4*s as the
// asymptotic worst case; empirically (probed via scratch_allocator high-water
// marks on sizes 550-80000 limbs in scratch_peak_bench) the actual peak/s
// ratio tops out at ~4.0016. 5*s leaves ~25% safety margin and matches the
// same generous-but-not-wasteful ratio used by Karatsuba and Toom-4.
constexpr std::size_t toom_cook_3_storage_size(const std::size_t s) noexcept { return 5 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 3-Way multiplication (Bodrato variant).
// Reference: Knuth TAOCP section 4.3.3
// Reference: Bodrato, "Towards Optimal Toom-Cook Multiplication" (2006)
//
// Splits each operand into three pieces of size k = ceil(max(an,bn)/3):
//   a = a2*B^(2k) + a1*B^k + a0,   b = b2*B^(2k) + b1*B^k + b0  (B = 2^limb_bits)
//
// Evaluates the product polynomial r(x) = p(x)*q(x) at five points
// {0, 1, -1, 2, infinity}, then interpolates the five coefficients c0-c4 of
// r(x) via Bodrato's seven-step in-place sequence (one /3, two /2).
// Result = c0 + c1*B^k + c2*B^(2k) + c3*B^(3k) + c4*B^(4k).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// ---------------------------------------------------------------------------
void multiply_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          const std::span<const uint_multiprecision_t> b_untrimmed,
                          scratch_allocator_base&                      scratch) noexcept;

// Minimum number of limbs for the Toom-Cook 3 squaring variant; roughly twice
// the general toom_cook_3_cutoff, mirroring the SQR/MUL threshold ratio.
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t square_toom_cook_3_cutoff = 400;

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_3: one evaluation per point
// instead of two, all five products are recursive squares, and p(-1)^2 >= 0
// removes the sign handling from interpolation. Falls back to
// square_karatsuba below the cutoff.
// `result` must be pre-zeroed and have space for 2 * a.size() limbs.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// ---------------------------------------------------------------------------
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it
// replaces `square_toom_cook_3_cutoff` for this call only; recursive
// sub-squares always use the default.
void square_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for Toom-Cook 4 to be worthwhile.
// Empirically tuned on Apple Silicon via multiplication_stress_bench
inline constexpr std::size_t toom_cook_4_cutoff = 1400;

// Heuristic estimate of scratch space needed for Toom-Cook 4 multiplication.
// One Toom-4 level uses 14k+16 limbs (~3.5*s where k = ceil(s/4)). The geometric
// sum over self-recursion converges to (14/3)*s ~= 4.67*s as the asymptotic
// worst case; empirically (probed via scratch_allocator high-water marks on
// sizes 1400-80000 limbs in scratch_peak_bench) the actual peak/s ratio ranges
// 4.46-4.66 (climbing monotonically toward the asymptote). 6*s leaves ~28%
// safety margin at the worst observed point and matches the same generous-but-
// not-wasteful ratio used by the smaller-radix algorithms.
constexpr std::size_t toom_cook_4_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 4-Way multiplication (Bodrato variant).
// Reference: Bodrato, "Towards Optimal Toom-Cook Multiplication" (2006).
//
// Splits each operand into four pieces of size k = ceil(max(an,bn)/4):
//   a = a3*B^(3k) + a2*B^(2k) + a1*B^k + a0,  b = b3*B^(3k) + ... (B = 2^limb_bits)
//
// Evaluates the product polynomial r(x) = p(x)*q(x) at seven points
// {0, 1, -1, 2, -2, 1/2 (scaled by 8), infinity}, then interpolates the seven
// coefficients c0..c6 of r(x) using a Bodrato-style in-place sequence with
// exact divisions by 2, 3, and 5.
// Result = c0 + c1*B^k + c2*B^(2k) + c3*B^(3k) + c4*B^(4k) + c5*B^(5k) + c6*B^(6k).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
//
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `toom_cook_4_cutoff` for this call only. Production callers should omit it so
// the default cutoff applies. Recursive sub-product calls always use the default.
// ---------------------------------------------------------------------------
void multiply_toom_cook_4(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          const std::span<const uint_multiprecision_t> b_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-Cook 4 squaring variant; roughly twice
// the general toom_cook_4_cutoff, mirroring the SQR/MUL threshold ratio.
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t square_toom_cook_4_cutoff = 2000;

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_4: one evaluation per point
// instead of two, all seven products are recursive squares, and the squared
// evaluations at -1 and -2 are non-negative, removing the sign handling from
// interpolation. Falls back to square_toom_cook_3 below the cutoff.
// `result` must be pre-zeroed and have space for 2 * a.size() limbs.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// `cutoff_override` is a benchmark-only escape hatch as in the general kernel.
// ---------------------------------------------------------------------------
void square_toom_cook_4(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override = 0) noexcept;

// See tests/beman/big_int/perf crossover_speedup.png
inline constexpr std::size_t toom_cook_6_5_cutoff = 3000;

// Heuristic estimate of scratch space needed for Toom-Cook 6.5 multiplication.
// One Toom-6.5 level uses 24k+26 limbs (~4*s where k = ceil(min/6)) for ten
// scratch products + two evaluation buffers + one tmp_double. The recursive
// child enters Toom-Cook 4 on pieces of size ~s/6, contributing roughly
// (Toom-4 asymptote)/6 ~= 4.67/6 ~= 0.78*s; the combined asymptote is
// ~4.78*s. Empirically (probed via scratch_allocator high-water marks on
// sizes 3000-80000 limbs in scratch_peak_bench) the actual peak/s ratio
// ranges 4.65-4.79 (climbing monotonically toward the asymptote). 6*s leaves
// ~25% safety margin at the worst observed point and matches the same
// generous-but-not-wasteful ratio used by the smaller-radix algorithms.
constexpr std::size_t toom_cook_6_5_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 6.5 ("Toom 6'n'half") multiplication (Bodrato variant).
// Reference: Bodrato, "High degree Toom'n'half for balanced and unbalanced
//            multiplication" (ARITH-20, 2011).
//
// Asymmetric split: the smaller operand is partitioned into 6 pieces (a, degree
// 5 polynomial) and the larger into up to 7 pieces (b, degree 6 polynomial)
// using a common piece size k = ceil(min(an, bn) / 6). For balanced operands
// b6 is empty and c11 = 0; for asymmetric operands with size ratio up to 7:6
// b6 is non-empty.
//
//   p(x) = a0 + a1*x + a2*x^2 + a3*x^3 + a4*x^4 + a5*x^5            (degree 5)
//   q(x) = b0 + b1*x + b2*x^2 + b3*x^3 + b4*x^4 + b5*x^5 + b6*x^6   (degree <= 6)
//   r(x) = p(x)*q(x) = c0 + c1*x + ... + c11*x^11                   (degree 11)
//
// Evaluates r at 12 points {0, +-1, +-2, +-4, +-1/2, +-1/4, +infinity}, then
// solves two 6x6 linear systems (one for even-index coefficients, one for odd)
// to recover c0..c11. c0 lives in result[0..2k); c11 (if non-zero) lives in
// result[11k..); the remaining ten coefficients are added back into result via
// add_shifted.
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
//
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `toom_cook_6_5_cutoff` for this call only. Production callers should omit it
// so the default cutoff applies. Recursive sub-product calls always use the
// default.
// ---------------------------------------------------------------------------
void multiply_toom_cook_6_5(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a_untrimmed,
                            const std::span<const uint_multiprecision_t> b_untrimmed,
                            scratch_allocator_base&                      scratch,
                            const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-6.5 squaring variant; roughly twice
// the general toom_cook_6_5_cutoff, mirroring the SQR/MUL threshold ratio.
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t square_toom_cook_6_5_cutoff = 2900;

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_6_5. Squaring is always balanced,
// so the operand splits into exactly six pieces (b6 of the general kernel is
// empty and c11 = 0). One evaluation per point instead of two; all eleven
// products are recursive squares. The squared fractional-point evaluations
// come out scaled by 2^10 / 4^10 instead of the 2^11 / 4^11 the interpolation
// expects (the general kernel pairs a 6-piece and a 7-piece evaluation), so
// vh/vmh are shifted left once and vq/vmq twice after squaring.
// Falls back to square_toom_cook_4 below the cutoff.
// `result` must be pre-zeroed and have space for 2 * a.size() limbs.
// `result` must NOT alias `a`. `scratch` provides pre-allocated workspace.
// `cutoff_override` is a benchmark-only escape hatch as in the general kernel.
// ---------------------------------------------------------------------------
void square_toom_cook_6_5(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// ---------------------------------------------------------------------------
// result <- a * p2 where p2 is a trimmed power of two: a shifted copy of `a`
// placed at limb offset p2.size() - 1, bit-shifted by countr_zero(p2.back()).
// O(n) with no scratch, versus a full kernel dispatch (see GMP mpz_mul_2exp).
// `result` must be pre-zeroed, have space for a.size() + p2.size() limbs, and
// must NOT alias `a`. Returns the number of significant result limbs.
// ---------------------------------------------------------------------------
constexpr std::size_t multiply_power_of_two(const std::span<uint_multiprecision_t>       result,
                                            const std::span<const uint_multiprecision_t> a,
                                            const std::span<const uint_multiprecision_t> p2) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(is_power_of_two_span(p2));
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + p2.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const std::size_t limb_offset = p2.size() - 1;
    const auto        bit_shift   = static_cast<unsigned>(std::countr_zero(p2.back()));
    const auto        dst         = result.subspan(limb_offset);

    std::ranges::copy(a, dst.begin());
    return limb_offset + shift_left_n(dst, a.size(), bit_shift);
}

// ---------------------------------------------------------------------------
// Top-level multiplication dispatcher.
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs.
// `result` must NOT alias `a` or `b`.
// Returns the number of significant result limbs (trimmed).
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr std::size_t multiply_dispatch(const std::span<uint_multiprecision_t>       result,
                                        const std::span<const uint_multiprecision_t> a_untrimmed,
                                        const std::span<const uint_multiprecision_t> b_untrimmed,
                                        Allocator&                                   alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a_untrimmed.size() + b_untrimmed.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Trivial case, use single-limb shortcuts
    if (a.size() == 1 && b.size() == 1) {
        const auto [lo, hi] = widening_mul(a[0], b[0]);
        result[0]           = lo;
        result[1]           = hi;
        return hi != 0 ? 2 : 1;
    }
    if (a.size() == 1) {
        return multiply_single_limb(result, b, a[0]);
    }
    if (b.size() == 1) {
        return multiply_single_limb(result, a, b[0]);
    }

    // This check has 0 runtime cost, but could speed up/reduce depth of constant evaluation
    if BEMAN_BIG_INT_IS_CONSTEVAL {
        if (is_power_of_two_span(b)) {
            return multiply_power_of_two(result, a, b);
        }
        if (is_power_of_two_span(a)) {
            return multiply_power_of_two(result, b, a);
        }
    }

    // Choose an algorithm to use based off the tuned cutoffs.
    // Avoid these at compile time because the recursion depth could blow up consteval limits;
    // long multiplication works just fine in that case.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        const std::size_t min_size = std::min(a.size(), b.size());
        if (min_size >= karatsuba_cutoff) {
            // Power-of-two operands reduce to a shifted copy of the other operand.
            // This is only worth checking if we're about to do a big number mul anyway
            if (is_power_of_two_span(b)) {
                return multiply_power_of_two(result, a, b);
            }
            if (is_power_of_two_span(a)) {
                return multiply_power_of_two(result, b, a);
            }

            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t result_total = a.size() + b.size();

            if (min_size < toom_cook_3_cutoff) {
                scratch_allocator<Allocator> scratch(karatsuba_storage_size(s), alloc);
                multiply_karatsuba(result.first(result_total), a, b, scratch);
            } else if (min_size < toom_cook_4_cutoff) {
                scratch_allocator<Allocator> scratch(toom_cook_3_storage_size(s), alloc);
                multiply_toom_cook_3(result.first(result_total), a, b, scratch);
            } else if (min_size < toom_cook_6_5_cutoff) {
                scratch_allocator<Allocator> scratch(toom_cook_4_storage_size(s), alloc);
                multiply_toom_cook_4(result.first(result_total), a, b, scratch);
            } else {
                scratch_allocator<Allocator> scratch(toom_cook_6_5_storage_size(s), alloc);
                multiply_toom_cook_6_5(result.first(result_total), a, b, scratch);
            }
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
    }

    // Long multiplication fallback
    multiply_long(result, a, b);
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), a.size() + b.size()});
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_MUL_IMPL_HPP
