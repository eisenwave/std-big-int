// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#ifndef BEMAN_BIG_INT_MUL_IMPL_HPP
#define BEMAN_BIG_INT_MUL_IMPL_HPP

#include <beman/big_int/detail/config.hpp>
#include <beman/big_int/detail/wide_ops.hpp>
#include <beman/big_int/detail/span_ops.hpp>
#include <beman/big_int/detail/scratch_allocator.hpp>

#include <algorithm>
#include <compare>
#include <memory>
#include <span>

namespace beman::big_int::detail {

// ---------------------------------------------------------------------------
// Long (classical) O(n*m) multiplication.
// `result` must be pre-zeroed and have space for `a.size() + b.size()` limbs.
// `result` must NOT alias `a` or `b`.
// ---------------------------------------------------------------------------
constexpr void multiply_long(const std::span<uint_multiprecision_t>       result,
                             const std::span<const uint_multiprecision_t> a,
                             const std::span<const uint_multiprecision_t> b) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + b.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b.data());

    // The key invariant from Boost is:
    //   double_limb_max - 2 * limb_max >= limb_max * limb_max
    // This means that: widening_mul(a[i], b[j]).high + carry + bool_carry
    // can never overflow a single limb, so we only need a single-limb carry.
    for (std::size_t i = 0; i < a.size(); ++i) {
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

// Minimum number of limbs for Karatsuba to be worthwhile
// Directly from Boost, and reconfirmed as correct
inline constexpr std::size_t karatsuba_cutoff = 40;

// Heuristic estimate of scratch space needed for Karatsuba multiplication.
// One Karatsuba level uses ~2*s limbs (t1=2n+2, t2=t3=n+1 with n=s/2+1). The
// geometric sum over self-recursion converges to 4*s as the asymptotic worst
// case; empirically (probed via scratch_allocator high-water marks on sizes
// 40-4000 limbs in scratch_peak_bench) the actual peak/s ratio tops out at
// ~3.997. 5*s leaves ~25% safety margin and matches the same generous-but-not-
// wasteful ratio used by the Toom-Cook variants.
constexpr std::size_t karatsuba_storage_size(const std::size_t s) noexcept { return 5 * s; }

// Maximum number of scratch limbs we're willing to put on the stack.
// Directly from Boost
inline constexpr std::size_t karatsuba_stack_threshold = 300;

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
                        scratch_allocator_base&                scratch) noexcept;

// ---------------------------------------------------------------------------
// Helpers shared by the Toom-Cook variants. All operate on little-endian
// multi-precision spans of uint_multiprecision_t limbs.
// ---------------------------------------------------------------------------

// In-place tmp[0..size) += addend; returns new size (may grow by 1).
[[nodiscard]] constexpr std::size_t add_into_tmp(const std::span<uint_multiprecision_t>       tmp,
                                                 const std::size_t                            size,
                                                 const std::span<const uint_multiprecision_t> addend) noexcept {
    if (addend.empty()) {
        return size;
    }

    const std::size_t new_size = std::max(size, addend.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(tmp.size() > new_size);

    const bool carry =
        add_unsigned_spans(tmp.first(new_size), std::span<const uint_multiprecision_t>{tmp.data(), size}, addend);

    if (carry) {
        tmp[new_size] = 1;
        return new_size + 1;
    }

    return new_size;
}

// In-place tmp[0..size) <<= n (0 < n < limb_width); returns new size (may grow by 1).
// Pass size == tmp.size() to shift the full span; the carry-branch assertion
// then enforces that the shift did not overflow.
// Single-pass equivalent of n chained shift_left_one calls.
[[nodiscard]] constexpr std::size_t
shift_left_n(const std::span<uint_multiprecision_t> tmp, std::size_t size, const unsigned n) noexcept {
    if (size == 0 || n == 0) {
        return size;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(size <= tmp.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    uint_multiprecision_t prev = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shl(wide<uint_multiprecision_t>{.low_bits = prev, .high_bits = limb}, n);
        prev            = limb;
    }

    if (const auto carry = prev >> (local_limb_bits - n)) {
        BEMAN_BIG_INT_DEBUG_ASSERT(size < tmp.size());
        tmp[size++] = carry;
    }

    return size;
}

// In-place tmp[0..size) <<= 1; returns new size (may grow by 1). Thin wrapper
// around shift_left_n for the common single-bit Horner-style shift.
[[nodiscard]] constexpr std::size_t shift_left_one(const std::span<uint_multiprecision_t> tmp,
                                                   const std::size_t                      size) noexcept {
    return shift_left_n(tmp, size, 1u);
}

// In-place tmp >>= n (0 < n < limb_width); returns the dropped low n bits packed
// as a single value (caller asserts == 0 for exact division by 2^n).
// Single-pass equivalent of n chained shift_right_one calls.
[[nodiscard]] constexpr uint_multiprecision_t shift_right_n(const std::span<uint_multiprecision_t> tmp,
                                                            const unsigned                         n) noexcept {
    if (tmp.empty() || n == 0) {
        return 0;
    }

    constexpr std::size_t local_limb_bits = width_v<uint_multiprecision_t>;
    BEMAN_BIG_INT_DEBUG_ASSERT(n < local_limb_bits);

    const uint_multiprecision_t mask = (uint_multiprecision_t{1} << n) - uint_multiprecision_t{1};
    const uint_multiprecision_t rem  = tmp[0] & mask;
    uint_multiprecision_t       high = 0;
    for (std::size_t i = tmp.size(); i-- > 0;) {
        const auto limb = tmp[i];
        tmp[i]          = funnel_shr(wide<uint_multiprecision_t>{.low_bits = limb, .high_bits = high}, n);
        high            = limb;
    }

    return rem;
}

// In-place tmp >>= 1; returns the dropped low bit (caller asserts == 0 for exact div).
// Thin wrapper around shift_right_n for the common single-bit halving case.
[[nodiscard]] constexpr uint_multiprecision_t shift_right_one(const std::span<uint_multiprecision_t> tmp) noexcept {
    return shift_right_n(tmp, 1u);
}

// In-place result[shift..) += src; asserts no carry out of the result span.
constexpr void add_shifted(const std::span<uint_multiprecision_t>       result,
                           const std::size_t                            shift,
                           const std::span<const uint_multiprecision_t> src) noexcept {
    const auto dest  = result.subspan(shift);
    bool       carry = false;
    for (std::size_t i = 0; i < dest.size(); ++i) {
        const auto si            = i < src.size() ? src[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(dest[i], si, carry);
        dest[i]                  = r_value;
        carry                    = c1;
    }

    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
}

// Given s = a + b and (|a - b|, sign_d) for non-negative a and b, recover the
// pair (a, b) and place them in (lower_dst, higher_dst):
//   sign_d == false  =>  a >= b: lower_dst = a = (s + |d|)/2; higher_dst = b = (s - |d|)/2
//   sign_d == true   =>  a <  b: lower_dst = a = (s - |d|)/2; higher_dst = b = (s + |d|)/2
// Used by Toom-Cook palindromic interpolation to split sum/diff back into
// individual coefficients. `tmp_scratch` is a workspace of at least
// max(s_view.size(), d_view.size()) limbs; it does not need to be pre-zeroed.
// The destinations may alias s_view and d_view (recover_pair reads s and d
// before writing the destinations); destinations must not alias each other
// nor tmp_scratch. Both s_view + d_view and s_view - d_view must be even
// (caller's responsibility, asserted via shift_right_one's rem == 0 check).
constexpr void recover_pair(const std::span<uint_multiprecision_t>       lower_dst,
                            const std::span<uint_multiprecision_t>       higher_dst,
                            const std::span<const uint_multiprecision_t> s_view,
                            const std::span<const uint_multiprecision_t> d_view,
                            const bool                                   sign_d,
                            const std::span<uint_multiprecision_t>       tmp_scratch) noexcept {
    // Stage (s + |d|) in tmp_scratch first; the subtract-into-destination step
    // below would otherwise clobber s_view / d_view if either aliases a destination.
    std::ranges::fill(tmp_scratch, uint_multiprecision_t{0});
    const bool sum_carry = add_unsigned_spans(tmp_scratch, s_view, d_view);
    BEMAN_BIG_INT_DEBUG_ASSERT(!sum_carry);
    const std::size_t plus_size = std::max(s_view.size(), d_view.size());
    const auto        plus_span = std::span<const uint_multiprecision_t>{tmp_scratch.data(), plus_size};

    // (s - |d|)/2 into the destination chosen by sign.
    const std::span<uint_multiprecision_t> minus_dst = sign_d ? lower_dst : higher_dst;
    subtract_unsigned_spans(minus_dst, s_view, d_view);
    {
        const auto rem = shift_right_one(minus_dst);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // (s + |d|)/2 into the other destination.
    const std::span<uint_multiprecision_t> plus_dst = sign_d ? higher_dst : lower_dst;
    std::ranges::fill(plus_dst, uint_multiprecision_t{0});
    std::ranges::copy(plus_span, plus_dst.begin());
    {
        const auto rem = shift_right_one(plus_dst);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
}

// Minimum number of limbs for Toom-Cook 3 to be worthwhile.
// See multiplication_stress_bench for tuning
inline constexpr std::size_t toom_cook_3_cutoff = 550;

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
                          scratch_allocator_base&                scratch) noexcept;

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

// Used by the solve_subsystem lambda in toom_cook_6_5
struct subsystem_signs {
    bool outer;
    bool inner;
};

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

    // Choose algorithm to use based off the tuned cutoffs.
    // Avoid these at compile time because the recursion depth could blow up consteval limits;
    // long multiplication works just fine in that case.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        const std::size_t min_size = std::min(a.size(), b.size());
        if (min_size >= karatsuba_cutoff) {
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
