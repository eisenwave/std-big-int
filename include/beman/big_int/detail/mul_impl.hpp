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
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

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
inline constexpr std::size_t karatsuba_fallback = 40;

// Heuristic estimate of scratch space needed for Karatsuba multiplication.
// One Karatsuba level uses ~2*s limbs (t1=2n+2, t2=t3=n+1 with n=s/2+1). The
// geometric sum over self-recursion converges to 4*s as the asymptotic worst
// case; empirically (probed via scratch_allocator high-water marks on sizes
// 40-4000 limbs in scratch_peak_bench) the actual peak/s ratio tops out at
// ~3.997. 5*s leaves ~25% safety margin and matches the same generous-but-not-
// wasteful ratio used by the Toom-Cook variants.
static_assert(karatsuba_fallback >= 5,
              "karatsuba_fallback < 5 makes the Karatsuba recursion non-terminating (stack overflow)");

constexpr std::size_t karatsuba_storage_size(const std::size_t s) noexcept { return 5 * s; }

// Maximum number of scratch limbs we are willing to place on the stack
// Value from boost
inline constexpr std::size_t karatsuba_stack_threshold = 300;

// ---------------------------------------------------------------------------
// Recursive Karatsuba multiplication.
// Port of Boost.Multiprecision multiply_karatsuba (lines 98-215).
//
// `result` must have space for a.size() + b.size() limbs or more.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// ---------------------------------------------------------------------------
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `karatsuba_fallback` for this call only (forcing a split at smaller sizes);
// recursive sub-products always use the default. Production callers omit it.
void multiply_karatsuba(const std::span<uint_multiprecision_t>       result,
                        const std::span<const uint_multiprecision_t> a_untrimmed,
                        const std::span<const uint_multiprecision_t> b_untrimmed,
                        scratch_allocator_base&                      scratch,
                        const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Karatsuba squaring variant: the squaring
// basecase stays ahead of recursion roughly twice as long as schoolbook does
// against general Karatsuba (the same SQR/MUL threshold ratio GMP observes).
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t square_karatsuba_cutoff = 72;

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

// Minimum number of limbs for Toom-Cook 3 to be worthwhile. Karatsuba still
// wins at 300-350 limbs (~15%); Toom-3 reliably overtakes from ~400.
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t toom_cook_3_cutoff = 400;

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
// `cutoff_override` is a benchmark-only escape hatch: when non-zero it replaces
// `toom_cook_3_cutoff` for this call only; recursive sub-products always use the
// default. Production callers omit it.
// ---------------------------------------------------------------------------
void multiply_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          const std::span<const uint_multiprecision_t> b_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-Cook 3 squaring variant; roughly twice
// the general toom_cook_3_cutoff, mirroring the SQR/MUL threshold ratio.
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t square_toom_cook_3_cutoff = 300;

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

// Minimum number of limbs for Toom-Cook 4 to be worthwhile. Toom-3 still wins
// at 1400 (~9%); Toom-4 reliably overtakes from ~1600.
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t toom_cook_4_cutoff = 1600;

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

// See tests/beman/big_int/perf crossover_speedup.png. Toom-6.5 overtakes Toom-4
// cleanly and monotonically from ~2400 limbs (re-measured 2026-06-04; the old
// 3000 left a ~2400-3000 band on the slower Toom-4).
// Tuned via multiplication_stress_bench.
inline constexpr std::size_t toom_cook_6_5_cutoff = 2400;

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
inline constexpr std::size_t square_toom_cook_6_5_cutoff = 2400;

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

// Minimum number of limbs for Toom-8.5 to be worthwhile. Measured via
// multiplication_stress_bench (two runs, AppleClang): below ~15000 Toom-6.5
// ties or wins; from 15000 Toom-8.5 overtakes cleanly and monotonically, and
// decisively (~5-8%) beyond ~24000.
inline constexpr std::size_t toom_cook_8_5_cutoff = 15000;

// Heuristic estimate of scratch space needed for Toom-Cook 8.5 multiplication.
// One Toom-8.5 level uses 32k+34 limbs (~4*s where k = ceil(min/8)) for fourteen
// scratch products + two evaluation buffers + one tmp_double. The recursive child
// enters Toom-6.5 (or Toom-8.5 above ~8x the cutoff) on pieces of size ~s/8.
// Empirically (BEMAN_BIG_INT_INSTRUMENT high-water probe over sizes 8000-130000,
// including two-level recursion) the peak/s ratio tops out at ~4.59 for the
// multiply kernel and ~4.43 for the square kernel. 6*s leaves ~24% margin and
// matches the ratio used by the smaller-radix algorithms.
constexpr std::size_t toom_cook_8_5_storage_size(const std::size_t s) noexcept { return 6 * s; }

// ---------------------------------------------------------------------------
// Recursive Toom-Cook 8.5 ("Toom 8'n'half") multiplication (Bodrato variant).
// Reference: Bodrato, "High degree Toom'n'half for balanced and unbalanced
//            multiplication" (ARITH-20, 2011); GMP mpn_toom8h_mul /
//            mpn_toom_interpolate_16pts (independent derivation; GMP is LGPL).
//
// Asymmetric Toom-9x8: the smaller operand is split into 8 pieces (degree-7 p)
// and the larger into up to 9 (degree-8 q), piece size k = ceil(min/8). Product
// r = p*q is degree 15. Evaluates r at 16 points
// {0, +-1, +-2, +-4, +-8, +-1/2, +-1/4, +-1/8, +infinity}, then solves two 7x7
// systems (even and odd parity) to recover c0..c15. c0 lives in result[0..2k);
// c15 = a7*b8 (zero for balanced inputs) lives in result[15k..).
//
// `result` must be pre-zeroed and have space for a.size() + b.size() limbs and
// must NOT alias `a` or `b`. `scratch` provides workspace. `cutoff_override` is
// the benchmark-only escape hatch; recursive sub-products use the default.
// Falls back to Toom-6.5 below the cutoff / outside the 9:8 ratio.
// ---------------------------------------------------------------------------
void multiply_toom_cook_8_5(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a_untrimmed,
                            const std::span<const uint_multiprecision_t> b_untrimmed,
                            scratch_allocator_base&                      scratch,
                            const std::size_t                            cutoff_override = 0) noexcept;

// Minimum number of limbs for the Toom-8.5 squaring variant. Measured via
// multiplication_stress_bench (two runs): square-Toom-6.5 stays competitive
// longer than the multiply kernel, with a reproducible ~1% square-Toom-8.5 dip
// near 20000, so the cutoff sits above it where 8.5 overtakes cleanly.
inline constexpr std::size_t square_toom_cook_8_5_cutoff = 24000;

// ---------------------------------------------------------------------------
// Squaring counterpart of multiply_toom_cook_8_5. Squaring is always balanced
// (the general kernel's b8 is empty and c15 = 0). One evaluation per point; all
// fourteen products are recursive squares; sign handling drops out. The squared
// fractional-point evaluations come out scaled by base^14 instead of the base^15
// the (9x8) interpolation expects, so vh/vmh are shifted left 1, vq/vmq left 2,
// and ve/vme left 3 after squaring. Falls back to square_toom_cook_6_5 below the
// cutoff.
// `result` must be pre-zeroed with space for 2 * a.size() limbs and must NOT
// alias `a`. `cutoff_override` is a benchmark-only escape hatch.
// ---------------------------------------------------------------------------
void square_toom_cook_8_5(const std::span<uint_multiprecision_t>       result,
                          const std::span<const uint_multiprecision_t> a_untrimmed,
                          scratch_allocator_base&                      scratch,
                          const std::size_t                            cutoff_override = 0) noexcept;

// ---------------------------------------------------------------------------
// FFT (small-prime NTT) multiplication: the asymptotically-best tier, above
// Toom-Cook 8.5. Operands are split into base-2^b coefficients, convolved via a
// number-theoretic transform modulo several word-size primes, recombined with the
// CRT, and carry-propagated into the product. Two implementations, selected at
// build time by BEMAN_BIG_INT_SIMD_MUL:
//   * default (macro undefined): an INTEGER Montgomery transform over two ~62-bit
//     primes (src/ntt.cpp, src/fft_mul.cpp). Pure integer arithmetic -- exact on
//     every conforming compiler, with no floating-point-environment assumptions.
//   * BEMAN_BIG_INT_SIMD_MUL defined: a double-precision FP transform over three
//     ~50-bit primes with hand-written NEON/AVX2 kernels and runtime dispatch
//     (src/ntt_fp_*.cpp, src/fft_mul_fp.cpp). Faster, but its exactness REQUIRES
//     round-to-nearest and no FMA-contraction / fast-math. The build applies
//     -ffp-contract=off (/fp:strict) to those TUs; opting in means accepting
//     responsibility for that floating-point environment in your own build system.
// ---------------------------------------------------------------------------

// Bits per packed coefficient (b) is chosen per multiply: a larger b means fewer
// coefficients -- a smaller, faster transform. The cap of 50 is the FP-exactness
// ceiling (a coefficient must be < 2^b <= 2^50 < a prime for the double-precision
// modmul) and is safe for the integer path too. 32 is the floor; any higher b is
// pure upside.
inline constexpr unsigned fft_max_coeff_bits = 50;
static_assert(fft_max_coeff_bits <= 50, "FP modmul exactness requires coefficients < 2^50");

// Number of base-2^b coefficients an n-limb operand splits into.
constexpr std::size_t fft_coeff_count(const std::size_t n_limbs, const unsigned b) noexcept {
    return (width_v<uint_multiprecision_t> * n_limbs + b - 1) / b; // ceil
}

// Bit budget for the CRT: a convolution coefficient must stay below the product of
// the NTT primes. Two ~62-bit primes (~2^124) for the integer path; three ~50-bit
// primes (~2^149) for the FP path.
#if defined(BEMAN_BIG_INT_SIMD_MUL)
inline constexpr std::size_t fft_crt_bits = 148;
#else
inline constexpr std::size_t fft_crt_bits = 123;
#endif

// Largest b in [32, fft_max_coeff_bits] for which an na-by-nb limb product's
// convolution coefficients provably fit the prime product. The widest coefficient
// sums min(na_coeff, nb_coeff) products of two b-bit values, so we need
// min_coeff * (2^b-1)^2 < prime product; bit_width(min_coeff) + 2b <= fft_crt_bits
// is a safe integer test. Always returns >= 32.
constexpr unsigned fft_choose_coeff_bits(const std::size_t na, const std::size_t nb) noexcept {
    const std::size_t min_limbs = na < nb ? na : nb;
    for (unsigned b = fft_max_coeff_bits; b > 32; --b) {
        const std::size_t min_coeff = fft_coeff_count(min_limbs, b);
        if (static_cast<std::size_t>(std::bit_width(min_coeff)) + 2 * b <= fft_crt_bits) {
            return b;
        }
    }
    return 32;
}

// Transform length for an na-by-nb limb product: the linear convolution has
// na_coeff + nb_coeff - 1 coefficients; round up to a power of two so the cyclic
// transform computes the linear convolution with no wraparound.
constexpr std::size_t fft_transform_length(const std::size_t na, const std::size_t nb) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, b) + fft_coeff_count(nb, b) - 1;
    return std::bit_ceil(result_coeff);
}

#if defined(BEMAN_BIG_INT_SIMD_MUL)
// FP path workspaces. Transform buffers are doubles (fca[N] + fcb[N] + ftw[N], the
// per-level twiddle table uses N-1); the three primes' residues are uint64
// (3 * result_coeff). N = fft_transform_length.
constexpr std::size_t fft_mul_fp_storage_size(const std::size_t na, const std::size_t nb) noexcept {
    return 3 * fft_transform_length(na, nb);
}
constexpr std::size_t fft_mul_int_storage_size(const std::size_t na, const std::size_t nb) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, b) + fft_coeff_count(nb, b) - 1;
    return 3 * result_coeff;
}
constexpr std::size_t square_fft_fp_storage_size(const std::size_t n_limbs) noexcept {
    return 2 * fft_transform_length(n_limbs, n_limbs);
}
constexpr std::size_t square_fft_int_storage_size(const std::size_t n_limbs) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(n_limbs, n_limbs);
    const std::size_t result_coeff = 2 * fft_coeff_count(n_limbs, b) - 1;
    return 3 * result_coeff;
}
#else
// Integer path workspace (std::uint64_t): ca[N] + cb[N] + tw[N/2] +
// res0[result_coeff] for multiply; ca[N] + tw[N/2] + save[result_coeff] for square.
constexpr std::size_t fft_mul_storage_size(const std::size_t na, const std::size_t nb) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(na, nb);
    const std::size_t n            = fft_transform_length(na, nb);
    const std::size_t result_coeff = fft_coeff_count(na, b) + fft_coeff_count(nb, b) - 1;
    return 2 * n + n / 2 + result_coeff;
}
constexpr std::size_t square_fft_storage_size(const std::size_t n_limbs) noexcept {
    const unsigned    b            = fft_choose_coeff_bits(n_limbs, n_limbs);
    const std::size_t n            = fft_transform_length(n_limbs, n_limbs);
    const std::size_t result_coeff = 2 * fft_coeff_count(n_limbs, b) - 1;
    return n + n / 2 + result_coeff;
}
#endif

// ---------------------------------------------------------------------------
// Cyclic NTT product sizes: a * b mod (2^(64w) - 1) computed with a
// transform of length L instead of the linear product's ~2L, by packing into
// uniform base 2^b with b * L == 64 * w exactly -- the transform's natural
// wraparound (mod x^L - 1) is then the value wraparound. Each output
// coefficient sums exactly L products of b-bit values, so the CRT bound
// uses L itself rather than the linear path's operand coefficient count.
// ---------------------------------------------------------------------------

BEMAN_BIG_INT_DIAGNOSTIC_PUSH()
BEMAN_BIG_INT_DIAGNOSTIC_IGNORED_GCC("-Wpadded")

struct fft_cyclic_params {
    std::size_t wrap_limbs; // w: the modulus is 2^(64w) - 1
    std::size_t length;     // L = 64 * w / b, a power of two
    unsigned    coeff_bits; // b, in [26, fft_max_coeff_bits]
};

BEMAN_BIG_INT_DIAGNOSTIC_POP()

// Smallest cyclic-capable wrap size >= min_w, with its packing. L ascends in
// powers of two (>= 64, so b * L is automatically a multiple of 64); for
// each L the smallest usable b is ceil(64 * min_w / L), admissible while it
// clears the cyclic CRT bound bit_width(L) + 2b <= fft_crt_bits. b lands in
// [26, 50] (the 26 floor keeps the search dense: the [32, 50] band's ratio
// is below 2, which would leave uncoverable wrap-size gaps); worst-case
// padding is under min_w / 25 (4%), and exactly zero when min_w is a power
// of two.
[[nodiscard]] constexpr fft_cyclic_params multiply_fft_cyclic_next_size(const std::size_t min_w) noexcept {
    constexpr std::size_t b_floor = 26;
    for (std::size_t length = 64;; length <<= 1) {
        const std::size_t b_cap = std::min<std::size_t>(
            fft_max_coeff_bits, (fft_crt_bits - static_cast<std::size_t>(std::bit_width(length))) / 2);
        const std::size_t b = std::max<std::size_t>(b_floor, (64 * min_w + length - 1) / length);
        if (b <= b_cap) {
            return {.wrap_limbs = b * (length / 64), .length = length, .coeff_bits = static_cast<unsigned>(b)};
        }
    }
}

// Compile-time properties the Barrett wiring relies on: growth, exact
// b*L == 64*w packing, the b range, idempotency (so a chooser size fed back
// in reproduces itself), the padding bound, and power-of-two lengths.
consteval bool fft_cyclic_next_size_properties() {
    for (std::size_t w = 1; w <= (std::size_t{1} << 22); w = w * 7 / 4 + 13) {
        const fft_cyclic_params p = multiply_fft_cyclic_next_size(w);
        const bool              ok =
            p.wrap_limbs >= w && 64 * p.wrap_limbs == static_cast<std::size_t>(p.coeff_bits) * p.length &&
            p.coeff_bits >= 26 && p.coeff_bits <= fft_max_coeff_bits && std::has_single_bit(p.length) &&
            multiply_fft_cyclic_next_size(p.wrap_limbs).wrap_limbs == p.wrap_limbs &&
            p.wrap_limbs < w + w / 25 + 64;
        if (!ok) {
            return false;
        }
    }
    return true;
}
static_assert(fft_cyclic_next_size_properties());

// Workspace sizes for the cyclic kernels (length-L transforms; the residue
// coefficient count equals L exactly).
#if defined(BEMAN_BIG_INT_SIMD_MUL)
constexpr std::size_t fft_cyclic_fp_storage_size(const fft_cyclic_params& p) noexcept { return 3 * p.length; }
constexpr std::size_t fft_cyclic_int_storage_size(const fft_cyclic_params& p) noexcept { return 3 * p.length; }
#else
constexpr std::size_t fft_cyclic_storage_size(const fft_cyclic_params& p) noexcept {
    return 2 * p.length + p.length / 2 + p.length;
}
#endif

// Minimum limb count (of the smaller operand) at which FFT overtakes Toom-Cook 8.5.
// These crossovers were measured with multiplication_stress_bench (release) on
// Apple Silicon (ARM64) and a native x86-64 box, and they vary strongly by BOTH the
// transform (integer vs the BEMAN_BIG_INT_SIMD_MUL FP NTT) and the architecture.
// FFT cost steps at power-of-two transform-length band boundaries, so each cutoff
// sits just above the boundary where FFT first wins for the bulk of that band:
//
//   config                     fft_mul   square_fft
//   integer, x86-64              24000      24000   x86's fast 64x64 mul makes Toom
//                                                   dominate the scalar NTT to ~24k
//   integer, AArch64 / other      4500       4500   NTT competitive with Toom here
//   FP (SIMD), x86-64 AVX2        6000      11000   AVX2 makes the FFT viable early
//   FP (SIMD), AArch64 NEON       6000       6000
//
// (On AArch64 the FP/NEON NTT is actually a little slower than the integer NTT -- the
// 3-prime FP transform costs more than the 2-prime integer one and NEON's 2-wide does
// not recover it -- so SIMD multiply mainly benefits x86-64.)
#if defined(BEMAN_BIG_INT_SIMD_MUL)
inline constexpr std::size_t fft_mul_cutoff = 6000;
    #if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t square_fft_cutoff = 11000;
    #else
inline constexpr std::size_t square_fft_cutoff = 6000;
    #endif
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
inline constexpr std::size_t fft_mul_cutoff    = 24000;
inline constexpr std::size_t square_fft_cutoff = 24000;
#else
inline constexpr std::size_t fft_mul_cutoff    = 4500;
inline constexpr std::size_t square_fft_cutoff = 4500;
#endif

// Entry size for the cyclic NTT tier of multiply_mod_bnm1: above it the
// wrapped product runs one length-L transform set instead of the CRT split,
// whose internal products fall below the linear FFT cutoff and surrender its
// advantage. Provisionally tied to the linear FFT cutoff per configuration;
// tuned via division_kernel_bench / division_stress_bench.
inline constexpr std::size_t fft_cyclic_cutoff = fft_mul_cutoff;

// FFT kernels. Operands may be untrimmed; `result` must have space for
// a.size()+b.size() limbs and must NOT alias `a`/`b`; it writes exactly that many
// (resp. 2*a.size()) limbs and the dispatcher trims. Workspaces are sized by the
// *_storage_size helpers above.
#if defined(BEMAN_BIG_INT_SIMD_MUL)
void multiply_fft(std::span<uint_multiprecision_t>       result,
                  std::span<const uint_multiprecision_t> a_untrimmed,
                  std::span<const uint_multiprecision_t> b_untrimmed,
                  std::span<double>                      fp_workspace,
                  std::span<std::uint64_t>               int_workspace) noexcept;

void square_fft(std::span<uint_multiprecision_t>       result,
                std::span<const uint_multiprecision_t> a_untrimmed,
                std::span<double>                      fp_workspace,
                std::span<std::uint64_t>               int_workspace) noexcept;

// Cyclic kernel: result (exactly params.wrap_limbs limbs) = a * b mod
// (2^(64w) - 1), semi-canonical (all-ones means zero). `params` must come
// from multiply_fft_cyclic_next_size, and the operands must be at most w
// limbs. 64-bit limbs only.
void multiply_fft_cyclic(std::span<uint_multiprecision_t>       result,
                         std::span<const uint_multiprecision_t> a_untrimmed,
                         std::span<const uint_multiprecision_t> b_untrimmed,
                         fft_cyclic_params                      params,
                         std::span<double>                      fp_workspace,
                         std::span<std::uint64_t>               int_workspace) noexcept;
#else
void multiply_fft(std::span<uint_multiprecision_t>       result,
                  std::span<const uint_multiprecision_t> a_untrimmed,
                  std::span<const uint_multiprecision_t> b_untrimmed,
                  std::span<std::uint64_t>               workspace) noexcept;

void square_fft(std::span<uint_multiprecision_t>       result,
                std::span<const uint_multiprecision_t> a_untrimmed,
                std::span<std::uint64_t>               workspace) noexcept;

// Cyclic kernel: result (exactly params.wrap_limbs limbs) = a * b mod
// (2^(64w) - 1), semi-canonical (all-ones means zero). `params` must come
// from multiply_fft_cyclic_next_size, and the operands must be at most w
// limbs. 64-bit limbs only.
void multiply_fft_cyclic(std::span<uint_multiprecision_t>       result,
                         std::span<const uint_multiprecision_t> a_untrimmed,
                         std::span<const uint_multiprecision_t> b_untrimmed,
                         fft_cyclic_params                      params,
                         std::span<std::uint64_t>               workspace) noexcept;
#endif

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
// Squaring dispatcher: result <- a * a using kernels that exploit the
// symmetric cross products. Same contract as multiply_dispatch: `result` must
// be pre-zeroed with space for 2 * a.size() limbs and must NOT alias `a`.
// `a` must be trimmed with at least two limbs. Returns the number of
// significant result limbs.
// ---------------------------------------------------------------------------
template <class Allocator>
std::size_t square_dispatch(const std::span<uint_multiprecision_t>       result,
                            const std::span<const uint_multiprecision_t> a,
                            Allocator&                                   alloc) {
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() >= 2);
    BEMAN_BIG_INT_DEBUG_ASSERT(a.back() != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= 2 * a.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    const std::size_t n            = a.size();
    const std::size_t result_total = 2 * n;

    // Tiny squares: plain schoolbook beats the three-pass squaring basecase.
    if (n < square_long_cutoff) {
        multiply_long(result.first(result_total), a, a);
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    // (2^k)^2 = 2^(2k): a shifted copy beats any squaring kernel.
    if (is_power_of_two_span(a)) {
        return multiply_power_of_two(result, a, a);
    }

    if (n < square_karatsuba_cutoff) {
        square_long(result, a);
        return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
    }

    if (n < square_toom_cook_3_cutoff) {
        scratch_allocator<Allocator> scratch(karatsuba_storage_size(n), alloc);
        square_karatsuba(result.first(result_total), a, scratch);
    } else if (n < square_toom_cook_4_cutoff) {
        scratch_allocator<Allocator> scratch(toom_cook_3_storage_size(n), alloc);
        square_toom_cook_3(result.first(result_total), a, scratch);
    } else if (n < square_toom_cook_6_5_cutoff) {
        scratch_allocator<Allocator> scratch(toom_cook_4_storage_size(n), alloc);
        square_toom_cook_4(result.first(result_total), a, scratch);
    } else {
        // n >= square_toom_cook_6_5_cutoff: Toom-6.5 / Toom-8.5, or FFT once it
        // overtakes at square_fft_cutoff. The FFT kernel packs into 64-bit words, so
        // it is gated to 64-bit limbs; on a 32-bit build the branch is discarded and
        // execution falls through to the Toom chain.
        bool used_fft = false;
        if constexpr (width_v<uint_multiprecision_t> == 64) {
            if (n >= square_fft_cutoff) {
                using u64_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<std::uint64_t>;
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                using f64_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<double>;
                std::vector<double, f64_alloc>        fp_ws(square_fft_fp_storage_size(n), f64_alloc(alloc));
                std::vector<std::uint64_t, u64_alloc> int_ws(square_fft_int_storage_size(n), u64_alloc(alloc));
                square_fft(result.first(result_total), a, fp_ws, int_ws);
#else
                std::vector<std::uint64_t, u64_alloc> ws(square_fft_storage_size(n), u64_alloc(alloc));
                square_fft(result.first(result_total), a, ws);
#endif
                used_fft = true;
            }
        }
        if (!used_fft) {
            if (n < square_toom_cook_8_5_cutoff) {
                scratch_allocator<Allocator> scratch(toom_cook_6_5_storage_size(n), alloc);
                square_toom_cook_6_5(result.first(result_total), a, scratch);
            } else {
                scratch_allocator<Allocator> scratch(toom_cook_8_5_storage_size(n), alloc);
                square_toom_cook_8_5(result.first(result_total), a, scratch);
            }
        }
    }

    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
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
        // Squaring halves the widening muls, and so the consteval step count.
        if (a.data() == b.data() && a.size() == b.size()) {
            square_long(result, a);
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), 2 * a.size()});
        }
    }

    // Choose an algorithm to use based off the tuned cutoffs.
    // Avoid these at compile time because the recursion depth could blow up consteval limits;
    // long multiplication works just fine in that case.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        // x * x and x *= x pass the same span twice, so squaring detection is
        // a pointer compare that almost always fails fast for ordinary mul
        if (a.data() == b.data() && a.size() == b.size()) {
            return square_dispatch(result, a, alloc);
        }

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
                const std::size_t storage_size = karatsuba_storage_size(s);
                if (storage_size <= karatsuba_stack_threshold) {
                    uint_multiprecision_t        stack_buf[karatsuba_stack_threshold];
                    scratch_allocator<Allocator> scratch(stack_buf, karatsuba_stack_threshold, alloc);
                    multiply_karatsuba(result.first(result_total), a, b, scratch);
                } else {
                    scratch_allocator<Allocator> scratch(storage_size, alloc);
                    multiply_karatsuba(result.first(result_total), a, b, scratch);
                }
            } else if (min_size < toom_cook_4_cutoff) {
                scratch_allocator<Allocator> scratch(toom_cook_3_storage_size(s), alloc);
                multiply_toom_cook_3(result.first(result_total), a, b, scratch);
            } else if (min_size < toom_cook_6_5_cutoff) {
                scratch_allocator<Allocator> scratch(toom_cook_4_storage_size(s), alloc);
                multiply_toom_cook_4(result.first(result_total), a, b, scratch);
            } else {
                // min_size >= toom_cook_6_5_cutoff: Toom-6.5 / Toom-8.5, or FFT
                // once it overtakes at fft_mul_cutoff. Gated to 64-bit limbs (the
                // FFT kernel packs into 64-bit words); on a 32-bit build the branch
                // is discarded and execution falls through to the Toom chain.
                bool used_fft = false;
                if constexpr (width_v<uint_multiprecision_t> == 64) {
                    if (min_size >= fft_mul_cutoff) {
                        using u64_alloc =
                            typename std::allocator_traits<Allocator>::template rebind_alloc<std::uint64_t>;
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                        using f64_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<double>;
                        std::vector<double, f64_alloc>        fp_ws(fft_mul_fp_storage_size(a.size(), b.size()),
                                                                    f64_alloc(alloc));
                        std::vector<std::uint64_t, u64_alloc> int_ws(fft_mul_int_storage_size(a.size(), b.size()),
                                                                     u64_alloc(alloc));
                        multiply_fft(result.first(result_total), a, b, fp_ws, int_ws);
#else
                        std::vector<std::uint64_t, u64_alloc> ws(fft_mul_storage_size(a.size(), b.size()),
                                                                 u64_alloc(alloc));
                        multiply_fft(result.first(result_total), a, b, ws);
#endif
                        used_fft = true;
                    }
                }
                if (!used_fft) {
                    if (min_size < toom_cook_8_5_cutoff) {
                        scratch_allocator<Allocator> scratch(toom_cook_6_5_storage_size(s), alloc);
                        multiply_toom_cook_6_5(result.first(result_total), a, b, scratch);
                    } else {
                        scratch_allocator<Allocator> scratch(toom_cook_8_5_storage_size(s), alloc);
                        multiply_toom_cook_8_5(result.first(result_total), a, b, scratch);
                    }
                }
            }
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
    }

    // Long multiplication fallback
    multiply_long(result, a, b);
    return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), a.size() + b.size()});
}

// ---------------------------------------------------------------------------
// Wraparound multiplication: a * b mod (B^w - 1), the GMP mulmod_bnm1
// lineage. B^w == 1 (mod B^w - 1), so the full 2w-limb product folds in half;
// computing it via the CRT split
//   mod (B^h - 1)  (recursive)   x   mod (B^h + 1)  (one h x h product)
// with a multiplication-free reassembly makes a wrapped product cost about
// half a full multiplication. Used by the Barrett division tier, where the
// subtrahend's true value is known to be small so only its residue matters.
// ---------------------------------------------------------------------------

// Below this wrap size the plain product plus a fold wins.
inline constexpr std::size_t multiply_mod_bnm1_cutoff = 16;

static_assert(multiply_mod_bnm1_cutoff >= 2, "the recursion must stop above single-limb wraps");

// Smallest wrap size >= n that the recursion can halve all the way down to
// the threshold without going odd (GMP's mpn_mulmod_bnm1_next_size shape).
// At and above fft_cyclic_cutoff (64-bit limbs) sizes come from the cyclic
// NTT chooser instead, so the wrapped product runs as one length-L
// transform set rather than the CRT split.
[[nodiscard]] constexpr std::size_t multiply_mod_bnm1_next_size(const std::size_t n,
                                                                const std::size_t threshold) noexcept {
    if (n <= threshold) {
        return n;
    }
    if constexpr (width_v<uint_multiprecision_t> == 64) {
        if (n >= fft_cyclic_cutoff) {
            return multiply_fft_cyclic_next_size(n).wrap_limbs;
        }
    }
    std::size_t chunk = 1;
    while ((n + chunk - 1) / chunk > threshold) {
        chunk <<= 1;
    }
    return ((n + chunk - 1) / chunk) * chunk;
}

// Scratch upper bound for multiply_mod_bnm1 at wrap size w: the recursion
// holds the two h-limb folded inputs across its recursive call, then frees
// them before the mod-(B^h + 1) stage's three (h+1)-limb values and 2h-limb
// product; the basecase needs the full 2w-limb product. Validated by the
// instrumented probe in mulmod_bnm1.test.cpp.
constexpr std::size_t multiply_mod_bnm1_storage_size(const std::size_t w) noexcept { return 3 * w + 16; }

// dst = src mod (B^w - 1) with w = dst.size(), semi-canonical (the all-ones
// pattern, equal to the modulus, may appear and means zero).
// Requires src.size() <= 2 * w.
constexpr void fold_mod_bnm1(const std::span<uint_multiprecision_t>       dst,
                             const std::span<const uint_multiprecision_t> src) noexcept {
    const std::size_t w = dst.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(src.size() <= 2 * w);

    const std::size_t lo = std::min(w, src.size());
    std::ranges::copy(src.first(lo), dst.begin());
    std::ranges::fill(dst.subspan(lo), uint_multiprecision_t{0});
    if (src.size() > w) {
        if (add_unsigned_spans(dst, dst, src.subspan(w))) {
            // B^w == 1: fold the carry back in; if that wraps too, the value
            // was B^w == 1 exactly.
            if (increment_span(dst)) {
                dst[0] = 1;
            }
        }
    }
}

// dst = (dst + addend) mod (B^w - 1) with w = dst.size(), semi-canonical.
// addend.size() <= w.
constexpr void add_mod_bnm1(const std::span<uint_multiprecision_t>       dst,
                            const std::span<const uint_multiprecision_t> addend) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(addend.size() <= dst.size());
    if (add_unsigned_spans(dst, dst, addend)) {
        if (increment_span(dst)) {
            dst[0] = 1;
        }
    }
}

// dst = src mod (B^h + 1) with h + 1 = dst.size(), canonical in [0, B^h]
// (so dst's top limb is 0 or 1, and 1 forces the rest to zero).
// Requires src.size() <= 2 * h.
constexpr void fold_mod_bnp1(const std::span<uint_multiprecision_t>       dst,
                             const std::span<const uint_multiprecision_t> src) noexcept {
    const std::size_t h = dst.size() - 1;
    BEMAN_BIG_INT_DEBUG_ASSERT(h >= 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(src.size() <= 2 * h);

    // B^h == -1: value = lo - hi.
    const auto lo = src.first(std::min(h, src.size()));
    const auto hi = src.size() > h ? src.subspan(h) : std::span<const uint_multiprecision_t>{};

    if (compare_unsigned_spans(lo, hi) != std::strong_ordering::less) {
        std::ranges::copy(lo, dst.begin());
        std::ranges::fill(dst.subspan(lo.size()), uint_multiprecision_t{0});
        subtract_unsigned_spans(dst.first(h), dst.first(h), hi);
        return;
    }

    // lo < hi: value = lo - hi + B^h + 1; the wrapped subtraction already
    // adds B^h, so only the +1 remains. If that carries the value is exactly
    // B^h (lo - hi == -1).
    std::ranges::copy(lo, dst.begin());
    std::ranges::fill(dst.subspan(lo.size()), uint_multiprecision_t{0});
    const bool borrow = subtract_unsigned_spans_borrow_out(dst.first(h), dst.first(h), hi);
    BEMAN_BIG_INT_DEBUG_ASSERT(borrow);
    dst[h] = increment_span(dst.first(h)) ? 1 : 0;
}

// r = a * b mod (B^h + 1) with h + 1 = r.size(); a and b canonical in
// [0, B^h] as produced by fold_mod_bnp1. One full h x h product plus a
// signed fold; operands equal to B^h itself (== -1) shortcut to a negation.
template <class Allocator>
void multiply_mod_bnp1(const std::span<uint_multiprecision_t>       r,
                       const std::span<const uint_multiprecision_t> a,
                       const std::span<const uint_multiprecision_t> b,
                       scratch_allocator_base&                      scratch,
                       Allocator&                                   alloc) {
    const std::size_t h = r.size() - 1;
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() == h + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() == h + 1);

    const auto negate_into = [&](const std::span<const uint_multiprecision_t> x) {
        // r = (B^h + 1) - x for x in (0, B^h], r = 0 for x == 0.
        if (is_span_zero(x)) {
            std::ranges::fill(r, uint_multiprecision_t{0});
            return;
        }
        std::ranges::fill(r, uint_multiprecision_t{0});
        r[0] = 1;
        r[h] = 1;
        subtract_unsigned_spans(r, r, x);
    };

    if (a[h] != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(is_span_zero(a.first(h)));
        negate_into(b);
        return;
    }
    if (b[h] != 0) {
        BEMAN_BIG_INT_DEBUG_ASSERT(is_span_zero(b.first(h)));
        negate_into(a);
        return;
    }

    const std::span<uint_multiprecision_t> prod = scratch.allocate(2 * h);
    std::ranges::fill(prod, uint_multiprecision_t{0});
    multiply_dispatch(prod, a.first(h), b.first(h), alloc);
    fold_mod_bnp1(r, prod);
    scratch.deallocate(2 * h);
}

// ---------------------------------------------------------------------------
// r = a * b mod (B^w - 1) with w = r.size(), semi-canonical (all-ones means
// zero). a.size() and b.size() must be at most w (fold larger operands
// first); r must not alias the inputs. `scratch` provides
// multiply_mod_bnm1_storage_size(w) limbs.
// Odd wrap sizes fall back to the plain product (size via
// multiply_mod_bnm1_next_size to keep the recursion even).
// `cutoff_override` is a test-only escape hatch forcing deep recursion.
// ---------------------------------------------------------------------------
template <class Allocator>
void multiply_mod_bnm1(const std::span<uint_multiprecision_t>       r,
                       const std::span<const uint_multiprecision_t> a,
                       const std::span<const uint_multiprecision_t> b,
                       scratch_allocator_base&                      scratch,
                       Allocator&                                   alloc,
                       const std::size_t                            cutoff_override = 0) {
    const std::size_t w = r.size();
    BEMAN_BIG_INT_DEBUG_ASSERT(w >= 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(!a.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(a.size() <= w);
    BEMAN_BIG_INT_DEBUG_ASSERT(b.size() <= w);
    BEMAN_BIG_INT_DEBUG_ASSERT(r.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(r.data() != b.data());

    const std::size_t cutoff = cutoff_override != 0 ? cutoff_override : multiply_mod_bnm1_cutoff;

    // Plain product when the wrap cannot engage (no wraparound, odd size, or
    // too small to be worth the CRT split).
    if (w <= cutoff || (w % 2) != 0 || a.size() + b.size() <= w) {
        const std::size_t                      p_len = a.size() + b.size();
        const std::span<uint_multiprecision_t> prod  = scratch.allocate(p_len);
        std::ranges::fill(prod, uint_multiprecision_t{0});
        multiply_dispatch(prod, a, b, alloc);
        fold_mod_bnm1(r, std::span<const uint_multiprecision_t>{prod.data(), p_len});
        scratch.deallocate(p_len);
        return;
    }

    // Cyclic NTT tier: one length-L transform set computes the wrapped
    // product directly when w is a chooser size (next_size produces exactly
    // these above the cutoff). Transform workspaces live on the heap like
    // multiply_dispatch's FFT branch, so the scratch model is untouched.
    // The test-only override keeps forcing the CRT recursion.
    if constexpr (width_v<uint_multiprecision_t> == 64) {
        if (cutoff_override == 0 && w >= fft_cyclic_cutoff) {
            const fft_cyclic_params params = multiply_fft_cyclic_next_size(w);
            if (params.wrap_limbs == w) {
                if (is_span_zero(a) || is_span_zero(b)) {
                    std::ranges::fill(r, uint_multiprecision_t{0});
                    return;
                }
                using u64_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<std::uint64_t>;
#if defined(BEMAN_BIG_INT_SIMD_MUL)
                using f64_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<double>;
                std::vector<double, f64_alloc>        fp_ws(fft_cyclic_fp_storage_size(params), f64_alloc(alloc));
                std::vector<std::uint64_t, u64_alloc> int_ws(fft_cyclic_int_storage_size(params), u64_alloc(alloc));
                multiply_fft_cyclic(r, a, b, params, fp_ws, int_ws);
#else
                std::vector<std::uint64_t, u64_alloc> ws(fft_cyclic_storage_size(params), u64_alloc(alloc));
                multiply_fft_cyclic(r, a, b, params, ws);
#endif
                return;
            }
        }
    }

    const std::size_t h = w / 2;

    // Half 1 (recursive): rm1 = a*b mod (B^h - 1), built into r's low half.
    {
        const std::span<uint_multiprecision_t> am1 = scratch.allocate(h);
        const std::span<uint_multiprecision_t> bm1 = scratch.allocate(h);
        fold_mod_bnm1(am1, a);
        fold_mod_bnm1(bm1, b);
        multiply_mod_bnm1(r.first(h),
                          std::span<const uint_multiprecision_t>{am1.data(), h},
                          std::span<const uint_multiprecision_t>{bm1.data(), h},
                          scratch,
                          alloc,
                          cutoff_override);
        scratch.deallocate(h);
        scratch.deallocate(h);
    }

    // Half 2: rp1 = a*b mod (B^h + 1).
    const std::span<uint_multiprecision_t> ap1 = scratch.allocate(h + 1);
    const std::span<uint_multiprecision_t> bp1 = scratch.allocate(h + 1);
    const std::span<uint_multiprecision_t> rp1 = scratch.allocate(h + 1);
    fold_mod_bnp1(ap1, a);
    fold_mod_bnp1(bp1, b);
    multiply_mod_bnp1(rp1,
                      std::span<const uint_multiprecision_t>{ap1.data(), h + 1},
                      std::span<const uint_multiprecision_t>{bp1.data(), h + 1},
                      scratch,
                      alloc);

    // CRT: r = rm1 + t * (B^h - 1) with t = (rm1 - rp1) / 2 mod (B^h + 1).
    // Reuse ap1's buffer for t.
    const std::span<uint_multiprecision_t> t = ap1;
    {
        std::ranges::copy(r.first(h), t.begin());
        t[h] = 0;
        if (compare_unsigned_spans(t.first(h), rp1) == std::strong_ordering::less) {
            // t = rm1 + (B^h + 1) before the subtraction.
            t[h] = increment_span(t.first(h)) ? 2 : 1;
        }
        subtract_unsigned_spans(t, t, rp1);
        if ((t[0] & 1u) != 0) {
            // Make the value even by adding B^h + 1 once more before halving.
            const bool wrapped = increment_span(t.first(h));
            t[h]               = t[h] + uint_multiprecision_t{1} + uint_multiprecision_t{wrapped};
        }
        const uint_multiprecision_t dropped = shift_right_n(t, 1u);
        BEMAN_BIG_INT_DEBUG_ASSERT(dropped == 0);
        BEMAN_BIG_INT_DEBUG_ASSERT(t[h] <= 1);
    }

    // Assemble in place: r = [rm1 | t_low] (+ 1 if t's top limb carries the
    // B^w == 1 wrap), then a modular subtraction of t.
    std::ranges::copy(t.first(h), r.begin() + static_cast<std::ptrdiff_t>(h));
    if (t[h] != 0) {
        if (increment_span(r)) {
            r[0] = 1;
        }
    }
    if (subtract_unsigned_spans_borrow_out(r, r, std::span<const uint_multiprecision_t>{t.data(), h + 1})) {
        // Wrapped past zero: -B^w == -1 (mod B^w - 1).
        [[maybe_unused]] const bool all_zero = decrement_span(r);
    }

    scratch.deallocate(h + 1);
    scratch.deallocate(h + 1);
    scratch.deallocate(h + 1);
}

} // namespace beman::big_int::detail

#endif // BEMAN_BIG_INT_MUL_IMPL_HPP
