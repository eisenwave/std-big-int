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
// Single-limb short division.
// `quotient[i]` := floor(([remainder, dividend[i]] as two limbs) / divisor)
// scanning from the top limb down.
// Returns the scalar remainder.
// `quotient` and `dividend` may be the same range (i.e. alias each other),
// but `quotient` may not be a strict subrange of `dividend`.
//
// Preconditions:
//   - divisor != 0
//   - quotient.size() >= dividend.size()
//   - dividend.size() >= 1
//   - quotient may alias dividend (we read dividend[i] before writing
//     quotient[i]; subsequent iterations touch strictly lower indices).
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr uint_multiprecision_t
divide_unsigned_short(const std::span<uint_multiprecision_t>       quotient,
                      const std::span<const uint_multiprecision_t> dividend,
                      const uint_multiprecision_t                  divisor) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(divisor != 0);
    BEMAN_BIG_INT_DEBUG_ASSERT(quotient.size() >= dividend.size());
    BEMAN_BIG_INT_DEBUG_ASSERT(!dividend.empty());

    uint_multiprecision_t remainder = 0;
    for (std::size_t i = dividend.size(); i-- > 0;) {
        // narrowing_div requires x.high_bits < y; the previous remainder was
        // taken mod divisor, so this invariant holds (and 0 < divisor on the
        // first iteration).
        const wide<uint_multiprecision_t> num{.low_bits = dividend[i], .high_bits = remainder};
        const auto [q, r] = narrowing_div(num, divisor);
        quotient[i]       = q;
        remainder         = r;
    }
    return remainder;
}

// ---------------------------------------------------------------------------
// Multiply a multi-limb number `a` by a single limb `val`.
// `result` must have space for `a.size() + 1` limbs and must NOT alias `a`.
// Returns the number of significant result limbs.
// ---------------------------------------------------------------------------
constexpr std::size_t multiply_single_limb(const std::span<uint_multiprecision_t>       result,
                                           const std::span<const uint_multiprecision_t> a,
                                           const uint_multiprecision_t                  val) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= a.size() + 1);
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a.data());

    uint_multiprecision_t carry = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto [lo, hi] = widening_mul(a[i], val);
        const auto [s1, c1] = carrying_add(lo, carry);
        result[i]           = s1;
        carry               = hi + static_cast<uint_multiprecision_t>(c1);
    }

    if (carry != 0) {
        result[a.size()] = carry;
        return a.size() + 1;
    }

    return a.size();
}

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

// ---------------------------------------------------------------------------
// Recursive Karatsuba multiplication.
// Port of Boost.Multiprecision multiply_karatsuba (lines 98-215).
//
// `result` must have space for a.size() + b.size() limbs or more.
// `result` must NOT alias `a` or `b`.
// `scratch` provides pre-allocated workspace for temporaries.
// ---------------------------------------------------------------------------
template <class Allocator>
constexpr void multiply_karatsuba(const std::span<uint_multiprecision_t>       result,
                                  const std::span<const uint_multiprecision_t> a_untrimmed,
                                  const std::span<const uint_multiprecision_t> b_untrimmed,
                                  scratch_allocator<Allocator>&                scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // First, check if we have enough limbs to justify karatsuba
    if (a.size() < karatsuba_cutoff || b.size() < karatsuba_cutoff) {
        std::ranges::fill(result, uint_multiprecision_t{0});
        multiply_long(result.first(a.size() + b.size()), a, b);
        return;
    }

    // Partition at n = max(a.size(), b.size()) / 2 + 1
    const std::size_t n = std::max(a.size(), b.size()) / 2 + 1;

    // Split: a = a_h * B^n + a_l,  b = b_h * B^n + b_l
    // where B = 2^bits_per_limb
    const auto a_l = a.first(std::min(a.size(), n));

    const uint_multiprecision_t zero{0U};
    const auto                  a_h = a.size() > n ? a.subspan(n) : std::span<const uint_multiprecision_t>(&zero, 1);

    const auto b_l = b.first(std::min(b.size(), n));
    const auto b_h = b.size() > n ? b.subspan(n) : std::span<const uint_multiprecision_t>(&zero, 1);

    // Allocate all temporaries in a single bump from scratch, then carve
    // sub-spans for each:
    //   t1: holds (a_h + a_l) * (b_h + b_l), needs up to 2*n + 2 limbs
    //   t2: holds a_h + a_l, needs up to n + 1 limbs
    //   t3: holds b_h + b_l, needs up to n + 1 limbs
    const std::size_t t1_cap        = 2 * n + 2;
    const std::size_t t2_cap        = n + 1;
    const std::size_t t3_cap        = n + 1;
    const std::size_t total_scratch = t1_cap + t2_cap + t3_cap;

    auto scratch_block = scratch.allocate(total_scratch);
    auto t1            = scratch_block.first(t1_cap);
    auto t2            = scratch_block.subspan(t1_cap, t2_cap);
    auto t3            = scratch_block.subspan(t1_cap + t2_cap, t3_cap);

    // result layout:
    //
    // result[0, 2*n) = result_low (will hold a_l * b_l)
    // result[2*n, result.size()) = result_high (will hold a_h * b_h)
    auto result_low  = result.first(2 * n);
    auto result_high = result.size() > 2 * n ? result.subspan(2 * n) : std::span<uint_multiprecision_t>{};

    // Compute result_low = a_l * b_l
    multiply_karatsuba(result_low, a_l, b_l, scratch);
    const std::size_t result_low_size =
        trimmed_size_span(std::span<const uint_multiprecision_t>{result_low.data(), a_l.size() + b_l.size()});

    // Zero unused limbs in result_low region
    std::ranges::fill(result_low.subspan(result_low_size), uint_multiprecision_t{0});

    // Compute result_high = a_h * b_h
    if (!result_high.empty()) {
        if ((a.size() > n) && (b.size() > n)) {
            multiply_karatsuba(result_high, a_h, b_h, scratch);

            const std::size_t result_high_size =
                trimmed_size_span(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});

            // Zero unused limbs in result_high region
            std::ranges::fill(result_high.subspan(result_high_size), uint_multiprecision_t{0});
        } else {
            result_high = std::span<uint_multiprecision_t>{};
        }
    }

    // Compute t2 = a_h + a_l
    std::size_t t2_size = std::max(a_h.size(), a_l.size());
    if (add_unsigned_spans(t2.first(t2_size), a_l, a_h)) {
        t2[t2_size] = 1;
        ++t2_size;
    }

    // Compute t3 = b_h + b_l
    std::size_t t3_size = std::max(b_h.size(), b_l.size());
    if (add_unsigned_spans(t3.first(t3_size), b_l, b_h)) {
        t3[t3_size] = 1;
        ++t3_size;
    }

    // Compute t1 = t2 * t3 = (a_h + a_l) * (b_h + b_l)
    std::ranges::fill(t1, uint_multiprecision_t{0});
    const auto t2_span = std::span<const uint_multiprecision_t>{t2.data(), t2_size};
    const auto t3_span = std::span<const uint_multiprecision_t>{t3.data(), t3_size};
    multiply_karatsuba(t1, t2_span, t3_span, scratch);
    std::size_t t1_size = trimmed_size_span(std::span<const uint_multiprecision_t>{t1.data(), t2_size + t3_size});

    // t1 -= result_high (a_h * b_h)
    if (!result_high.empty()) {
        const std::size_t rh_size =
            trimmed_size_span(std::span<const uint_multiprecision_t>{result_high.data(), a_h.size() + b_h.size()});
        t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                          std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                          std::span<const uint_multiprecision_t>{result_high.data(), rh_size});
    }

    // t1 -= result_low (a_l * b_l)
    t1_size = subtract_unsigned_spans(t1.first(t1_size),
                                      std::span<const uint_multiprecision_t>{t1.data(), t1_size},
                                      std::span<const uint_multiprecision_t>{result_low.data(), result_low_size});

    // Add t1 shifted left by n limbs into result: result[n...] += t1
    const auto result_mid = result.subspan(n);
    bool       carry      = false;
    for (std::size_t i = 0; i < result_mid.size(); ++i) {
        const auto ti            = i < t1_size ? t1[i] : uint_multiprecision_t{0};
        const auto [r_value, c1] = carrying_add(result_mid[i], ti, carry);
        result_mid[i]            = r_value;
        carry                    = c1;
    }
    BEMAN_BIG_INT_DEBUG_ASSERT(!carry);

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    // No actual deallocation happens,
    // this is pointer arithmetic within a single pre-allocated buffer.
    scratch.deallocate(total_scratch);
}

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
    [[maybe_unused]] const bool sum_carry = add_unsigned_spans(tmp_scratch, s_view, d_view);
    BEMAN_BIG_INT_DEBUG_ASSERT(!sum_carry);
    const std::size_t plus_size = std::max(s_view.size(), d_view.size());
    const auto        plus_span = std::span<const uint_multiprecision_t>{tmp_scratch.data(), plus_size};

    // (s - |d|)/2 into the destination chosen by sign.
    const std::span<uint_multiprecision_t> minus_dst = sign_d ? lower_dst : higher_dst;
    subtract_unsigned_spans(minus_dst, s_view, d_view);
    {
        [[maybe_unused]] const auto rem = shift_right_one(minus_dst);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // (s + |d|)/2 into the other destination.
    const std::span<uint_multiprecision_t> plus_dst = sign_d ? higher_dst : lower_dst;
    std::ranges::fill(plus_dst, uint_multiprecision_t{0});
    std::ranges::copy(plus_span, plus_dst.begin());
    {
        [[maybe_unused]] const auto rem = shift_right_one(plus_dst);
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
template <class Allocator>
constexpr void multiply_toom_cook_3(const std::span<uint_multiprecision_t>       result,
                                    const std::span<const uint_multiprecision_t> a_untrimmed,
                                    const std::span<const uint_multiprecision_t> b_untrimmed,
                                    scratch_allocator<Allocator>&                scratch) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Partition at k = ceil(max(an, bn) / 3).
    const std::size_t min_size = std::min(a.size(), b.size());
    const std::size_t k        = (std::max(a.size(), b.size()) + 2) / 3;

    // Fall through to Karatsuba (and on through to schoolbook) when the smaller
    // operand is below the performance cutoff or below the algorithm's 2*k
    // invariant (Toom-Cook 3 needs both a2 and b2 non-empty).
    if (min_size < toom_cook_3_cutoff || min_size <= 2 * k) {
        multiply_karatsuba(result, a, b, scratch);
        return;
    }

    // Split each operand into three pieces. Empty pieces represent zero.
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 = a.size() > 2 * k ? a.subspan(2 * k) : std::span<const uint_multiprecision_t>{};

    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 = b.size() > 2 * k ? b.subspan(2 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each
    //   v1, vm1, v2: 2k+2 limbs each, hold three product values that survive through interpolation
    //   v0 = a0*b0 and vinf = a2*b2 live directly in result.
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 3 * prod_cap;

    auto block = scratch.allocate(total_scratch);
    auto tmpa  = block.first(tmp_cap);
    auto tmpb  = block.subspan(tmp_cap, tmp_cap);
    auto v1    = block.subspan(2 * tmp_cap, prod_cap);
    auto vm1   = block.subspan(2 * tmp_cap + prod_cap, prod_cap);
    auto v2    = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);

    // ---- v0 = a0*b0, written into result[0..2k) ----
    // result is pre-zeroed by the caller convention, so result.first(2k) is too.
    multiply_toom_cook_3(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- vinf = a2*b2, written into result[4k..) ----
    if (!a2.empty() && !b2.empty()) {
        multiply_toom_cook_3(result.subspan(4 * k, a2.size() + b2.size()), a2, b2, scratch);
    }

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2; tmpb = b0 + b1 + b2 ----
    std::ranges::copy(a0, tmpa.begin());
    std::size_t tmpa_size = a0.size();
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a2);

    std::ranges::copy(b0, tmpb.begin());
    std::size_t tmpb_size = b0.size();
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b2);

    // v1 = tmpa * tmpb
    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_3(v1,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -1: tmpa = (a0 + a2) - a1 (signed); tmpb similarly ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    const auto sub_a =
        subtract_unsigned_spans_signed(tmpa, std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size}, a1);
    tmpa_size         = sub_a.size;
    const bool sign_a = sub_a.negative;

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    const auto sub_b =
        subtract_unsigned_spans_signed(tmpb, std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size}, b1);
    tmpb_size         = sub_b.size;
    const bool sign_b = sub_b.negative;

    // Sign of vm1 = sign of (p(-1)*q(-1)). Magnitude = |p(-1)| * |q(-1)|.
    const bool sign_vm1 = sign_a ^ sign_b;

    // vm1 = |p(-1)| * |q(-1)|
    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size == 0 || tmpb_size == 0) {
        // One of the evaluations is zero, so vm1 = 0.
    } else {
        multiply_toom_cook_3(vm1,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 2: tmpa = 4*a2 + 2*a1 + a0 (Horner: ((a2<<1)+a1)<<1+a0); tmpb similarly ----
    std::ranges::copy(a2, tmpa.begin());
    tmpa_size = a2.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);   // 2*a2
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1); // 2*a2 + a1
    tmpa_size = shift_left_one(tmpa, tmpa_size);   // 4*a2 + 2*a1
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0); // 4*a2 + 2*a1 + a0

    std::ranges::copy(b2, tmpb.begin());
    tmpb_size = b2.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    // v2 = p(2) * q(2)
    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_3(v2,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Bodrato interpolation (in-place; full 2k+2 buffers throughout) ----
    // After this sequence: v0=c0 (in result), vm1=c1, v1=c2, v2=c3, vinf=c4 (in result).
    //
    // Clip vinf_view to its semantic size of 2k limbs: c4 = a2*b2 with a2.size() <= k
    // and b2.size() <= k yields a product of at most 2k limbs. The result span may
    // extend past 4k+2k when called recursively from a parent whose prod_cap buffer
    // is larger than the actual product (slack = caller-pre-zeroed). Subtracting
    // those slack zeros is correct, but the size assertion in subtract_unsigned_spans
    // would fail without clipping.
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 4 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 4 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};

    // Step 1: v2 <- (v2 - vm1) / 3 (sign-aware: add if vm1 was negative).
    if (sign_vm1) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v2, v2_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(v2, v2_view, vm1_view);
    }
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 2: vm1 <- (v1 - vm1) / 2 (sign-aware). After this, vm1 is non-negative.
    if (sign_vm1) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm1, v1_view, vm1_view);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(vm1);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 3: v1 <- v1 - v0 (sizes: v1 is 2k+2, v0_view is 2k; v1 >= v0 numerically).
    subtract_unsigned_spans(v1, v1_view, v0_view);

    // Step 4: v2 <- (v2 - v1) / 2.
    subtract_unsigned_spans(v2, v2_view, v1_view);
    {
        [[maybe_unused]] const auto rem = shift_right_one(v2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 5: v1 <- v1 - vm1 - vinf.
    subtract_unsigned_spans(v1, v1_view, vm1_view);
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 6: v2 <- v2 - 2*vinf (subtract twice, no doubling helper needed).
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    subtract_unsigned_spans(v2, v2_view, vinf_view);

    // Step 7: vm1 <- vm1 - v2.
    subtract_unsigned_spans(vm1, vm1_view, v2_view);

    // ---- Recompose: result += vm1*B^k + v1*B^(2k) + v2*B^(3k) ----
    // c0 already at result[0..2k); c4 already at result[4k..). Add the three middle
    // coefficients with carry chains spanning to result.size() so carries can
    // propagate into the c4 region.
    add_shifted(result, k, vm1_view);
    add_shifted(result, 2 * k, v1_view);
    add_shifted(result, 3 * k, v2_view);

    // Move bump pointer back so the next sibling recursive call reuses the same region.
    scratch.deallocate(total_scratch);
}

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
template <class Allocator>
constexpr void multiply_toom_cook_4(const std::span<uint_multiprecision_t>       result,
                                    const std::span<const uint_multiprecision_t> a_untrimmed,
                                    const std::span<const uint_multiprecision_t> b_untrimmed,
                                    scratch_allocator<Allocator>&                scratch,
                                    const std::size_t                            cutoff_override = 0) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Partition at k = ceil(max(an, bn) / 4).
    const std::size_t min_size         = std::min(a.size(), b.size());
    const std::size_t k                = (std::max(a.size(), b.size()) + 3) / 4;
    const std::size_t effective_cutoff = cutoff_override == 0 ? toom_cook_4_cutoff : cutoff_override;

    // Fall through to Toom-Cook 3 (and on through Karatsuba and schoolbook) when the
    // smaller operand is below the performance cutoff or below the algorithm's 3*k
    // invariant (Toom-Cook 4 needs both a3 and b3 non-empty).
    if (min_size < effective_cutoff || min_size <= 3 * k) {
        multiply_toom_cook_3(result, a, b, scratch);
        return;
    }

    // Split each operand into four pieces.
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 =
        a.size() > 2 * k ? a.subspan(2 * k, std::min(k, a.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a3 = a.size() > 3 * k ? a.subspan(3 * k) : std::span<const uint_multiprecision_t>{};

    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 =
        b.size() > 2 * k ? b.subspan(2 * k, std::min(k, b.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b3 = b.size() > 3 * k ? b.subspan(3 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each (evaluation buffers, hold p(x) and q(x) at each point)
    //   v1, vm1, v2, vm2, vh: 2k+2 limbs each (five interpolation buffers)
    //   tmp_double: 2k+2 limbs (used for in-place bit shifts to replace doubling with mul)
    // c0 = a0*b0 lives in result[0..2k); c6 = a3*b3 lives in result[6k..).
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 6 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto tmpb       = block.subspan(tmp_cap, tmp_cap);
    auto v1         = block.subspan(2 * tmp_cap, prod_cap);
    auto vm1        = block.subspan(2 * tmp_cap + prod_cap, prod_cap);
    auto v2         = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(2 * tmp_cap + 3 * prod_cap, prod_cap);
    auto vh         = block.subspan(2 * tmp_cap + 4 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(2 * tmp_cap + 5 * prod_cap, prod_cap);

    // ---- c0 = a0*b0, written into result[0..2k) (caller pre-zeroed). ----
    multiply_toom_cook_4(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- c6 = a3*b3, written into result[6k..). ----
    // The min > 3*k gate above guarantees both a3 and b3 are non-empty.
    multiply_toom_cook_4(result.subspan(6 * k, a3.size() + b3.size()), a3, b3, scratch);

    // ---- Evaluate at x = 1: tmpa = a0 + a1 + a2 + a3; tmpb similarly. ----
    std::ranges::copy(a0, tmpa.begin());
    std::size_t tmpa_size = a0.size();
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size             = add_into_tmp(tmpa, tmpa_size, a3);

    std::ranges::copy(b0, tmpb.begin());
    std::size_t tmpb_size = b0.size();
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size             = add_into_tmp(tmpb, tmpb_size, b3);

    // v1 = tmpa * tmpb
    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_4(v1,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -1: tmpa = (a0 + a2) - (a1 + a3) signed; tmpb similarly. ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    // tmpb temporarily used to hold a1 + a3 before subtraction.
    std::ranges::copy(a1, tmpb.begin());
    std::size_t aux_size = a1.size();
    aux_size             = add_into_tmp(tmpb, aux_size, a3);
    const auto sub_a_m1 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{tmpb.data(), aux_size});
    tmpa_size            = sub_a_m1.size;
    const bool sign_a_m1 = sub_a_m1.negative;

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    // tmpa now holds (a0 + a2) - (a1 + a3); reuse tail of vm1 as a scratch slot for (b1 + b3).
    std::ranges::copy(b1, vm1.begin());
    aux_size = b1.size();
    aux_size = add_into_tmp(vm1, aux_size, b3);
    const auto sub_b_m1 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpb_size            = sub_b_m1.size;
    const bool sign_b_m1 = sub_b_m1.negative;
    const bool sign_vm1  = sign_a_m1 ^ sign_b_m1;

    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_4(vm1,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 2: tmpa = ((a3*2 + a2)*2 + a1)*2 + a0 (Horner); tmpb similarly. ----
    std::ranges::copy(a3, tmpa.begin());
    tmpa_size = a3.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);

    std::ranges::copy(b3, tmpb.begin());
    tmpb_size = b3.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_4(v2,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Evaluate at x = -2: tmpa = (a0 + 4*a2) - (2*a1 + 8*a3) signed; tmpb similarly.
    // Build positive = a0 + 4*a2 in tmpa, negative = 2*a1 + 8*a3 in tmpb, then signed-sub.
    std::ranges::copy(a2, tmpa.begin());
    tmpa_size = a2.size();
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);

    std::ranges::copy(a3, tmpb.begin());
    aux_size = a3.size();
    aux_size = shift_left_n(tmpb, aux_size, 3u);
    aux_size = add_into_tmp(tmpb, aux_size, a1);
    // tmpb holds 8*a3 + a1; we still need 2*a1, which is (8*a3 + a1) + a1 = 8*a3 + 2*a1.
    aux_size = add_into_tmp(tmpb, aux_size, a1);

    const auto sub_a_m2 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{tmpb.data(), aux_size});
    tmpa_size            = sub_a_m2.size;
    const bool sign_a_m2 = sub_a_m2.negative;

    std::ranges::copy(b2, tmpb.begin());
    tmpb_size = b2.size();
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    // Use vm2 as a scratch slot for 8*b3 + 2*b1.
    std::ranges::copy(b3, vm2.begin());
    aux_size = b3.size();
    aux_size = shift_left_n(vm2, aux_size, 3u);
    aux_size = add_into_tmp(vm2, aux_size, b1);
    aux_size = add_into_tmp(vm2, aux_size, b1);

    const auto sub_b_m2 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpb_size            = sub_b_m2.size;
    const bool sign_b_m2 = sub_b_m2.negative;
    const bool sign_vm2  = sign_a_m2 ^ sign_b_m2;

    std::ranges::fill(vm2, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_4(vm2,
                             std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                             std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                             scratch);
    }

    // ---- Evaluate at x = 1/2 (scaled by 8): tmpa = ((a0*2 + a1)*2 + a2)*2 + a3 (Horner);
    // this equals 8*a(1/2), so the resulting product is 64*c(1/2). ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a3);

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b3);

    std::ranges::fill(vh, uint_multiprecision_t{0});
    multiply_toom_cook_4(vh,
                         std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                         std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                         scratch);

    // ---- Bodrato-style interpolation (in-place; full 2k+2 buffers throughout) ----
    // After this sequence the c-coefficients live as:
    //   c0 in result[0..2k), c1 in vh, c2 in v1, c3 in vm1, c4 in v2, c5 in vm2, c6 in result[6k..).
    //
    // v0_view and vinf_view bound the c0 and c6 regions in result. Clip vinf_view because
    // the result span may be larger than 8k when the caller's prod_cap exceeds the actual
    // product (same trick as Toom-3 at line 642).
    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = std::min(result.size() - 6 * k, 2 * k);
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 6 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};
    const auto vm2_view  = std::span<const uint_multiprecision_t>{vm2};
    const auto vh_view   = std::span<const uint_multiprecision_t>{vh};
    const auto td_view   = std::span<const uint_multiprecision_t>{tmp_double};

    // Phase 1: Symmetrize {v1, vm1} into {E1, D1} and {v2, vm2} into {E2, D2}.
    //   E1 = (v1 + vm1)/2 = c0 + c2 + c4 + c6
    //   D1 = (v1 - vm1)/2 = c1 + c3 + c5
    //   E2 = (v2 + vm2)/2 = c0 + 4c2 + 16c4 + 64c6
    //   D2 = (v2 - vm2)/4 = c1 + 4c3 + 16c5

    // Step 1a: v1 <- (v1 + vm1) algebraic (sign-aware on sign_vm1).
    if (sign_vm1) {
        subtract_unsigned_spans(v1, v1_view, vm1_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    // Step 1b: v1 /= 2.
    {
        [[maybe_unused]] const auto rem = shift_right_one(v1);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 1: v1 = E1 = c0 + c2 + c4 + c6.

    // Step 2: vm1 <- v1 - vm1 algebraic = (v1_orig - vm1_orig)/2 = D1.
    if (sign_vm1) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm1, v1_view, vm1_view);
    }
    // After Step 2: vm1 = D1 = c1 + c3 + c5. sign_vm1 is no longer needed.

    // Step 3a: v2 <- (v2 + vm2) algebraic.
    if (sign_vm2) {
        subtract_unsigned_spans(v2, v2_view, vm2_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    // Step 3b: v2 /= 2.
    {
        [[maybe_unused]] const auto rem = shift_right_one(v2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 3: v2 = E2 = c0 + 4c2 + 16c4 + 64c6.

    // Step 4a: vm2 <- v2 - vm2 algebraic = (v2_orig - vm2_orig)/2 = 2c1 + 8c3 + 32c5.
    if (sign_vm2) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm2, v2_view, vm2_view);
    }
    // Step 4b: vm2 /= 2.
    {
        [[maybe_unused]] const auto rem = shift_right_one(vm2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // After Step 4: vm2 = D2 = c1 + 4c3 + 16c5. sign_vm2 is no longer needed.

    // Phase 2: Solve even system for c2 (into v1) and c4 (into v2).

    // Step 5: v2 -= v1.  v2 = (c0+4c2+16c4+64c6) - (c0+c2+c4+c6) = 3*(c2 + 5c4 + 21c6).
    subtract_unsigned_spans(v2, v2_view, v1_view);
    // Step 6: v2 /= 3.  v2 = c2 + 5c4 + 21c6.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(v2, v2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 7: v1 -= c0.  v1 = c2 + c4 + c6.
    subtract_unsigned_spans(v1, v1_view, v0_view);
    // Step 8: v1 -= c6.  v1 = c2 + c4.
    subtract_unsigned_spans(v1, v1_view, vinf_view);

    // Step 9: v2 -= v1.  v2 = 4c4 + 21c6.
    subtract_unsigned_spans(v2, v2_view, v1_view);

    // Step 10: subtract 21*c6 from v2 using tmp_double for the doublings.
    //   v2 -= 1*c6, then v2 -= 4*c6, then v2 -= 16*c6.  Total subtracted = 21*c6.
    subtract_unsigned_spans(v2, v2_view, vinf_view);
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vinf_view, tmp_double.begin());
    std::size_t td_size = vinf_view.size();
    td_size             = shift_left_n(tmp_double, td_size, 2u);
    subtract_unsigned_spans(v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    td_size = shift_left_n(tmp_double, td_size, 2u);
    subtract_unsigned_spans(v2, v2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    // After Step 10: v2 = 4c4.

    // Step 11: v2 /= 4 (two halvings).  v2 = c4.
    {
        [[maybe_unused]] const auto rem = shift_right_n(v2, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 12: v1 -= v2.  v1 = c2.
    subtract_unsigned_spans(v1, v1_view, v2_view);

    // Phase 3: Reduce vh to T_odd = (vh - 64c0 - 16c2 - 4c4 - c6) / 2 = 16c1 + 4c3 + c5.

    // Step 13: vh -= 64*c0 (subtract 64*c0 via doubled-tmp_double).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    td_size = trimmed_size_span(v0_view);
    td_size = shift_left_n(tmp_double, td_size, 6u);
    subtract_unsigned_spans(vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // Step 14: vh -= 16*c2 (c2 lives in v1 now).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v1_view, tmp_double.begin());
    td_size = trimmed_size_span(v1_view);
    td_size = shift_left_n(tmp_double, td_size, 4u);
    subtract_unsigned_spans(vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // Step 15: vh -= 4*c4 (c4 lives in v2 now).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v2_view, tmp_double.begin());
    td_size = trimmed_size_span(v2_view);
    td_size = shift_left_n(tmp_double, td_size, 2u);
    subtract_unsigned_spans(vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // Step 16: vh -= c6.
    subtract_unsigned_spans(vh, vh_view, vinf_view);

    // Step 17: vh /= 2.  vh = 16c1 + 4c3 + c5 = T_odd.
    {
        [[maybe_unused]] const auto rem = shift_right_one(vh);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 4: Solve odd system using vm1 = D1, vm2 = D2, vh = T_odd.
    //   vm2 -= D1:  3c3 + 15c5
    //   vh  -= D1: 15c1 +  3c3
    //   then divide both by 3.

    // Step 18: vm2 -= vm1.  vm2 = 3*(c3 + 5c5).
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    // Step 19: vh -= vm1.  vh = 3*(5c1 + c3).
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    // Step 20: vm2 /= 3.  vm2 = alpha = c3 + 5c5.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    // Step 21: vh /= 3.  vh = beta = 5c1 + c3.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Now vm1 = D1, vm2 = alpha, vh = beta. Recover c3 = (5*D1 - alpha - beta) / 3 into vm1.
    // Step 22: tmp_double = D1; vm1 *= 4; vm1 += tmp_double  (-> 5*D1).
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vm1_view, tmp_double.begin());
    [[maybe_unused]] const auto sz_2D1 = shift_left_one(vm1, vm1.size()); // 2*D1
    BEMAN_BIG_INT_DEBUG_ASSERT(sz_2D1 == vm1.size());
    [[maybe_unused]] const auto sz_4D1 = shift_left_one(vm1, vm1.size()); // 4*D1
    BEMAN_BIG_INT_DEBUG_ASSERT(sz_4D1 == vm1.size());
    {
        [[maybe_unused]] const bool carry = add_unsigned_spans(vm1, vm1_view, td_view); // 5*D1
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
    }
    // Step 23: vm1 -= alpha; vm1 -= beta.  vm1 = 3*c3.
    subtract_unsigned_spans(vm1, vm1_view, vm2_view);
    subtract_unsigned_spans(vm1, vm1_view, vh_view);
    // Step 24: vm1 /= 3.  vm1 = c3.
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vm1, vm1_view, uint_multiprecision_t{3});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 25: vh -= c3 -> 5*c1; vh /= 5.  vh = c1.
    subtract_unsigned_spans(vh, vh_view, vm1_view);
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vh, vh_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Step 26: vm2 -= c3 -> 5*c5; vm2 /= 5.  vm2 = c5.
    subtract_unsigned_spans(vm2, vm2_view, vm1_view);
    {
        [[maybe_unused]] const auto rem = divide_unsigned_short(vm2, vm2_view, uint_multiprecision_t{5});
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Phase 5: Recompose.
    //   result += vh * B^k        (c1)
    //   result += v1 * B^(2k)     (c2)
    //   result += vm1 * B^(3k)    (c3)
    //   result += v2 * B^(4k)     (c4)
    //   result += vm2 * B^(5k)    (c5)
    // c0 and c6 are already in result.
    // Carries propagate up to result.size().
    add_shifted(result, k, vh_view);
    add_shifted(result, 2 * k, v1_view);
    add_shifted(result, 3 * k, vm1_view);
    add_shifted(result, 4 * k, v2_view);
    add_shifted(result, 5 * k, vm2_view);

    // Release scratch back to the bump pool for sibling reuse.
    scratch.deallocate(total_scratch);
}

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
template <class Allocator>
constexpr void multiply_toom_cook_6_5(const std::span<uint_multiprecision_t>       result,
                                      const std::span<const uint_multiprecision_t> a_untrimmed,
                                      const std::span<const uint_multiprecision_t> b_untrimmed,
                                      scratch_allocator<Allocator>&                scratch,
                                      const std::size_t                            cutoff_override = 0) noexcept {
    BEMAN_BIG_INT_DEBUG_ASSERT(!a_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(!b_untrimmed.empty());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.size() >= trimmed_size_span(a_untrimmed) + trimmed_size_span(b_untrimmed));
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != a_untrimmed.data());
    BEMAN_BIG_INT_DEBUG_ASSERT(result.data() != b_untrimmed.data());

    const auto a_trim = a_untrimmed.first(trimmed_size_span(a_untrimmed));
    const auto b_trim = b_untrimmed.first(trimmed_size_span(b_untrimmed));

    // Orient: a is the smaller (6 pieces), b is the larger (up to 7 pieces).
    // Multiplication is commutative so we can freely reorder the operands here.
    const auto a = a_trim.size() <= b_trim.size() ? a_trim : b_trim;
    const auto b = a_trim.size() <= b_trim.size() ? b_trim : a_trim;

    const std::size_t min_size         = a.size();
    const std::size_t max_size         = b.size();
    const std::size_t k                = (min_size + 5) / 6; // ceil(min/6)
    const std::size_t effective_cutoff = cutoff_override == 0 ? toom_cook_6_5_cutoff : cutoff_override;

    // Fallback to Toom-Cook 4 when:
    //   - the smaller operand is below the performance cutoff, OR
    //   - the algorithm's 5*k invariant would leave a5 empty, OR
    //   - the size ratio exceeds 7:6 so b doesn't fit in seven pieces.
    if (min_size < effective_cutoff || min_size <= 5 * k || max_size > 7 * k) {
        multiply_toom_cook_4(result, a_trim, b_trim, scratch);
        return;
    }

    // Split span a into six pieces of size k (a5 may be partial).
    const auto a0 = a.first(std::min(k, a.size()));
    const auto a1 = a.size() > k ? a.subspan(k, std::min(k, a.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto a2 =
        a.size() > 2 * k ? a.subspan(2 * k, std::min(k, a.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a3 =
        a.size() > 3 * k ? a.subspan(3 * k, std::min(k, a.size() - 3 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a4 =
        a.size() > 4 * k ? a.subspan(4 * k, std::min(k, a.size() - 4 * k)) : std::span<const uint_multiprecision_t>{};
    const auto a5 = a.size() > 5 * k ? a.subspan(5 * k) : std::span<const uint_multiprecision_t>{};

    // Split b into up to seven pieces of size k (b5, b6 may be partial; b6 may be empty).
    const auto b0 = b.first(std::min(k, b.size()));
    const auto b1 = b.size() > k ? b.subspan(k, std::min(k, b.size() - k)) : std::span<const uint_multiprecision_t>{};
    const auto b2 =
        b.size() > 2 * k ? b.subspan(2 * k, std::min(k, b.size() - 2 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b3 =
        b.size() > 3 * k ? b.subspan(3 * k, std::min(k, b.size() - 3 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b4 =
        b.size() > 4 * k ? b.subspan(4 * k, std::min(k, b.size() - 4 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b5 =
        b.size() > 5 * k ? b.subspan(5 * k, std::min(k, b.size() - 5 * k)) : std::span<const uint_multiprecision_t>{};
    const auto b6 = b.size() > 6 * k ? b.subspan(6 * k) : std::span<const uint_multiprecision_t>{};

    // Carve scratch:
    //   tmpa, tmpb: k+2 limbs each (evaluation buffers; +2 limbs for growth from
    //               scaling factors up to 4^6 = 12 bits in the q-side reverse Horner).
    //   v1, vm1, v2, vm2, v4, vm4, vh, vmh, vq, vmq: 2k+2 limbs each (ten product buffers).
    //   tmp_double: 2k+2 limbs (scratch for in-place scaling during interpolation).
    // c0 = a0*b0 lives in result[0..2k); c11 = a5*b6 (when b6 non-empty) lives in result[11k..).
    const std::size_t tmp_cap       = k + 2;
    const std::size_t prod_cap      = 2 * k + 2;
    const std::size_t total_scratch = 2 * tmp_cap + 11 * prod_cap;

    auto block      = scratch.allocate(total_scratch);
    auto tmpa       = block.first(tmp_cap);
    auto tmpb       = block.subspan(tmp_cap, tmp_cap);
    auto v1         = block.subspan(2 * tmp_cap + 0 * prod_cap, prod_cap);
    auto vm1        = block.subspan(2 * tmp_cap + 1 * prod_cap, prod_cap);
    auto v2         = block.subspan(2 * tmp_cap + 2 * prod_cap, prod_cap);
    auto vm2        = block.subspan(2 * tmp_cap + 3 * prod_cap, prod_cap);
    auto v4         = block.subspan(2 * tmp_cap + 4 * prod_cap, prod_cap);
    auto vm4        = block.subspan(2 * tmp_cap + 5 * prod_cap, prod_cap);
    auto vh         = block.subspan(2 * tmp_cap + 6 * prod_cap, prod_cap);
    auto vmh        = block.subspan(2 * tmp_cap + 7 * prod_cap, prod_cap);
    auto vq         = block.subspan(2 * tmp_cap + 8 * prod_cap, prod_cap);
    auto vmq        = block.subspan(2 * tmp_cap + 9 * prod_cap, prod_cap);
    auto tmp_double = block.subspan(2 * tmp_cap + 10 * prod_cap, prod_cap);

    // ---- c0 = a0*b0, written into result[0..2k). Caller pre-zeroed result. ----
    multiply_toom_cook_6_5(result.first(a0.size() + b0.size()), a0, b0, scratch);

    // ---- c11 = a5*b6, written into result[11k..). Skipped when b6 is empty
    // (balanced inputs): result[11k..) is already zero, so c11 = 0 is correct.
    if (!b6.empty()) {
        multiply_toom_cook_6_5(result.subspan(11 * k, a5.size() + b6.size()), a5, b6, scratch);
    }

    std::size_t tmpa_size = 0;
    std::size_t tmpb_size = 0;
    std::size_t aux_size  = 0;

    // ---- Evaluate at x = 1: tmpa = a0+a1+a2+a3+a4+a5; tmpb = b0+b1+...+b6. ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    // TODO(mborland) : Much like we now have shift_left_n can we do add_many_into_tmp?
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a3);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a5);

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b3);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b5);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b6);

    std::ranges::fill(v1, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(v1,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -1 (signed):
    //   tmpa = (a0+a2+a4) - (a1+a3+a5);  tmpb = (b0+b2+b4+b6) - (b1+b3+b5). ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    // Use vm1 (currently zero) as scratch to hold (a1+a3+a5).
    std::ranges::copy(a1, vm1.begin());
    aux_size = a1.size();
    aux_size = add_into_tmp(vm1, aux_size, a3);
    aux_size = add_into_tmp(vm1, aux_size, a5);
    const auto sub_a_m1 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpa_size            = sub_a_m1.size;
    const bool sign_a_m1 = sub_a_m1.negative;

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b6);
    // Reuse vm1 as scratch for (b1+b3+b5).
    std::ranges::copy(b1, vm1.begin());
    aux_size = b1.size();
    aux_size = add_into_tmp(vm1, aux_size, b3);
    aux_size = add_into_tmp(vm1, aux_size, b5);
    const auto sub_b_m1 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm1.data(), aux_size});
    tmpb_size            = sub_b_m1.size;
    const bool sign_b_m1 = sub_b_m1.negative;
    const bool sign_vm1  = sign_a_m1 ^ sign_b_m1;

    std::ranges::fill(vm1, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vm1,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 2: tmpa = 32a5+16a4+8a3+4a2+2a1+a0 (Horner from a5);
    //                         tmpb = 64b6+32b5+...+b0 (Horner from b6). ----
    // TODO(mborland): Same thing, super repetitive calls can be handled better?
    std::ranges::copy(a5, tmpa.begin());
    tmpa_size = a5.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a3);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);

    std::ranges::copy(b6, tmpb.begin());
    tmpb_size = b6.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b5);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b3);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    std::ranges::fill(v2, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(v2,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -2 (signed):
    //   tmpa = (a0+4a2+16a4) - (2a1+8a3+32a5);
    //   tmpb = (b0+4b2+16b4+64b6) - (2b1+8b3+32b5). ----
    std::ranges::copy(a4, tmpa.begin());
    tmpa_size = a4.size();
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);
    // negative part 2a1+8a3+32a5 into vm2 (currently zero).
    std::ranges::copy(a5, vm2.begin());
    aux_size = a5.size();
    aux_size = shift_left_n(vm2, aux_size, 2u);
    aux_size = add_into_tmp(vm2, aux_size, a3);
    aux_size = shift_left_n(vm2, aux_size, 2u);
    aux_size = add_into_tmp(vm2, aux_size, a1);
    aux_size = shift_left_one(vm2, aux_size);
    const auto sub_a_m2 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpa_size            = sub_a_m2.size;
    const bool sign_a_m2 = sub_a_m2.negative;

    // b-side: positive part b0+4b2+16b4+64b6 into tmpb.
    std::ranges::copy(b6, tmpb.begin());
    tmpb_size = b6.size();
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);
    // negative part 2b1+8b3+32b5 into vm2 (reusing as scratch).
    std::ranges::copy(b5, vm2.begin());
    aux_size = b5.size();
    aux_size = shift_left_n(vm2, aux_size, 2u);
    aux_size = add_into_tmp(vm2, aux_size, b3);
    aux_size = shift_left_n(vm2, aux_size, 2u);
    aux_size = add_into_tmp(vm2, aux_size, b1);
    aux_size = shift_left_one(vm2, aux_size);
    const auto sub_b_m2 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm2.data(), aux_size});
    tmpb_size            = sub_b_m2.size;
    const bool sign_b_m2 = sub_b_m2.negative;
    const bool sign_vm2  = sign_a_m2 ^ sign_b_m2;

    std::ranges::fill(vm2, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vm2,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 4: tmpa = 1024a5 + 256a4 + 64a3 + 16a2 + 4a1 + a0
    //                         (Horner with two shifts per step). ----
    std::ranges::copy(a5, tmpa.begin());
    tmpa_size = a5.size();
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a3);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);

    std::ranges::copy(b6, tmpb.begin());
    tmpb_size = b6.size();
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b5);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b3);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);

    std::ranges::fill(v4, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(v4,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -4 (signed):
    //   tmpa = (a0+16a2+256a4) - (4a1+64a3+1024a5);
    //   tmpb = (b0+16b2+256b4+4096b6) - (4b1+64b3+1024b5). ----
    std::ranges::copy(a4, tmpa.begin());
    tmpa_size = a4.size();
    tmpa_size = shift_left_n(tmpa, tmpa_size, 4u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 4u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a0);
    // negative part 4a1+64a3+1024a5 into vm4.
    std::ranges::copy(a5, vm4.begin());
    aux_size = a5.size();
    aux_size = shift_left_n(vm4, aux_size, 4u);
    aux_size = add_into_tmp(vm4, aux_size, a3);
    aux_size = shift_left_n(vm4, aux_size, 4u);
    aux_size = add_into_tmp(vm4, aux_size, a1);
    aux_size = shift_left_n(vm4, aux_size, 2u);
    const auto sub_a_m4 =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vm4.data(), aux_size});
    tmpa_size            = sub_a_m4.size;
    const bool sign_a_m4 = sub_a_m4.negative;

    std::ranges::copy(b6, tmpb.begin());
    tmpb_size = b6.size();
    tmpb_size = shift_left_n(tmpb, tmpb_size, 4u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 4u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 4u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b0);
    // negative part 4b1+64b3+1024b5 into vm4 (reusing).
    std::ranges::copy(b5, vm4.begin());
    aux_size = b5.size();
    aux_size = shift_left_n(vm4, aux_size, 4u);
    aux_size = add_into_tmp(vm4, aux_size, b3);
    aux_size = shift_left_n(vm4, aux_size, 4u);
    aux_size = add_into_tmp(vm4, aux_size, b1);
    aux_size = shift_left_n(vm4, aux_size, 2u);
    const auto sub_b_m4 =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vm4.data(), aux_size});
    tmpb_size            = sub_b_m4.size;
    const bool sign_b_m4 = sub_b_m4.negative;
    const bool sign_vm4  = sign_a_m4 ^ sign_b_m4;

    std::ranges::fill(vm4, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vm4,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 1/2 (scaled by 2^11): reverse Horner from a0/b0.
    //   tmpa = 32a0+16a1+8a2+4a3+2a4+a5 = 32*p(1/2);
    //   tmpb = 64b0+32b1+16b2+8b3+4b4+2b5+b6 = 64*q(1/2);
    //   vh = tmpa*tmpb = 2048*r(1/2). ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a3);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a5);

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b3);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b5);
    tmpb_size = shift_left_one(tmpb, tmpb_size);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b6);

    std::ranges::fill(vh, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(vh,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -1/2 (scaled by 2^11), signed.
    //   |32*p(-1/2)| = |(32a0+8a2+2a4) - (16a1+4a3+a5)|;
    //   |64*q(-1/2)| = |(64b0+16b2+4b4+b6) - (32b1+8b3+2b5)|.
    // Note: p has odd degree (5), so p_rev(-2) = -32*p(-1/2). We flip the raw
    // XOR sign at the end so sign_vmh tracks sign(2048*r(-1/2)) directly. ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    tmpa_size = shift_left_one(tmpa, tmpa_size);
    // tmpa = 32a0+8a2+2a4
    // negative: 16a1+4a3+a5 in vmh.
    std::ranges::copy(a1, vmh.begin());
    aux_size = a1.size();
    aux_size = shift_left_n(vmh, aux_size, 2u);
    aux_size = add_into_tmp(vmh, aux_size, a3);
    aux_size = shift_left_n(vmh, aux_size, 2u);
    aux_size = add_into_tmp(vmh, aux_size, a5);
    const auto sub_a_mh =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vmh.data(), aux_size});
    tmpa_size            = sub_a_mh.size;
    const bool sign_a_mh = sub_a_mh.negative;

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b6);
    // negative: 32b1+8b3+2b5 in vmh (reusing).
    std::ranges::copy(b1, vmh.begin());
    aux_size = b1.size();
    aux_size = shift_left_n(vmh, aux_size, 2u);
    aux_size = add_into_tmp(vmh, aux_size, b3);
    aux_size = shift_left_n(vmh, aux_size, 2u);
    aux_size = add_into_tmp(vmh, aux_size, b5);
    aux_size = shift_left_one(vmh, aux_size);
    const auto sub_b_mh =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vmh.data(), aux_size});
    tmpb_size            = sub_b_mh.size;
    const bool sign_b_mh = sub_b_mh.negative;
    // tmpa stores |32*p(-1/2)| with sign_a_mh; tmpb stores |64*q(-1/2)| with sign_b_mh.
    // Their product is 2048*r(-1/2) signed, so sign_vmh tracks sign(r(-1/2)) directly.
    const bool sign_vmh = sign_a_mh ^ sign_b_mh;

    std::ranges::fill(vmh, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vmh,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Evaluate at x = 1/4 (scaled by 4^11): reverse Horner with x4 per step.
    //   tmpa = 1024a0+256a1+64a2+16a3+4a4+a5;
    //   tmpb = 4096b0+1024b1+256b2+64b3+16b4+4b5+b6. ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a1);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a3);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a5);

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b1);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b3);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b5);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 2u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b6);

    std::ranges::fill(vq, uint_multiprecision_t{0});
    multiply_toom_cook_6_5(vq,
                           std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                           std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                           scratch);

    // ---- Evaluate at x = -1/4 (scaled by 4^11), signed.
    //   |1024a0+64a2+4a4 - (256a1+16a3+a5)|;
    //   |4096b0+256b2+16b4+b6 - (1024b1+64b3+4b5)|.
    // As with vmh, the (-4)^5 factor in p_rev introduces a sign flip; absorb at the end. ----
    std::ranges::copy(a0, tmpa.begin());
    tmpa_size = a0.size();
    tmpa_size = shift_left_n(tmpa, tmpa_size, 4u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a2);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 4u);
    tmpa_size = add_into_tmp(tmpa, tmpa_size, a4);
    tmpa_size = shift_left_n(tmpa, tmpa_size, 2u);
    // tmpa = 1024a0 + 64a2 + 4a4
    std::ranges::copy(a1, vmq.begin());
    aux_size = a1.size();
    aux_size = shift_left_n(vmq, aux_size, 4u);
    aux_size = add_into_tmp(vmq, aux_size, a3);
    aux_size = shift_left_n(vmq, aux_size, 4u);
    aux_size = add_into_tmp(vmq, aux_size, a5);
    // vmq = 256a1 + 16a3 + a5
    const auto sub_a_mq =
        subtract_unsigned_spans_signed(tmpa,
                                       std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                                       std::span<const uint_multiprecision_t>{vmq.data(), aux_size});
    tmpa_size            = sub_a_mq.size;
    const bool sign_a_mq = sub_a_mq.negative;

    std::ranges::copy(b0, tmpb.begin());
    tmpb_size = b0.size();
    tmpb_size = shift_left_n(tmpb, tmpb_size, 4u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b2);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 4u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b4);
    tmpb_size = shift_left_n(tmpb, tmpb_size, 4u);
    tmpb_size = add_into_tmp(tmpb, tmpb_size, b6);
    // tmpb = 4096b0 + 256b2 + 16b4 + b6
    std::ranges::copy(b1, vmq.begin());
    aux_size = b1.size();
    aux_size = shift_left_n(vmq, aux_size, 4u);
    aux_size = add_into_tmp(vmq, aux_size, b3);
    aux_size = shift_left_n(vmq, aux_size, 4u);
    aux_size = add_into_tmp(vmq, aux_size, b5);
    aux_size = shift_left_n(vmq, aux_size, 2u);
    // vmq = 1024b1 + 64b3 + 4b5
    const auto sub_b_mq =
        subtract_unsigned_spans_signed(tmpb,
                                       std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                                       std::span<const uint_multiprecision_t>{vmq.data(), aux_size});
    tmpb_size            = sub_b_mq.size;
    const bool sign_b_mq = sub_b_mq.negative;
    // Same convention as vmh: signed tmpa*tmpb = 4194304*r(-1/4), no flip needed.
    const bool sign_vmq = sign_a_mq ^ sign_b_mq;

    std::ranges::fill(vmq, uint_multiprecision_t{0});
    if (tmpa_size != 0 && tmpb_size != 0) {
        multiply_toom_cook_6_5(vmq,
                               std::span<const uint_multiprecision_t>{tmpa.data(), tmpa_size},
                               std::span<const uint_multiprecision_t>{tmpb.data(), tmpb_size},
                               scratch);
    }

    // ---- Interpolation ----
    // After 12 evaluations and the sign-aware folds below, each pair (v_x, vm_x)
    // collapses into (E_x, D_x) holding the even/odd parts of r(x). Five even
    // equations + c0 (known) recover c2..c10; five odd equations + c11 (known)
    // recover c1..c9. The same 5x5 matrix appears in both subsystems so the
    // elimination sequence below applies to v's and vm's in parallel.

    const auto v0_view   = std::span<const uint_multiprecision_t>{result.data(), 2 * k};
    const auto vinf_size = result.size() > 11 * k ? std::min(result.size() - 11 * k, 2 * k) : std::size_t{0};
    const auto vinf_view = std::span<const uint_multiprecision_t>{result.data() + 11 * k, vinf_size};
    const auto v1_view   = std::span<const uint_multiprecision_t>{v1};
    const auto vm1_view  = std::span<const uint_multiprecision_t>{vm1};
    const auto v2_view   = std::span<const uint_multiprecision_t>{v2};
    const auto vm2_view  = std::span<const uint_multiprecision_t>{vm2};
    const auto v4_view   = std::span<const uint_multiprecision_t>{v4};
    const auto vm4_view  = std::span<const uint_multiprecision_t>{vm4};
    const auto vh_view   = std::span<const uint_multiprecision_t>{vh};
    const auto vmh_view  = std::span<const uint_multiprecision_t>{vmh};
    const auto vq_view   = std::span<const uint_multiprecision_t>{vq};
    const auto vmq_view  = std::span<const uint_multiprecision_t>{vmq};

    // -- Phase 1: symmetrize each pair. v_x <- E_x; vm_x <- D_x (with the
    // /(2x) factor absorbed for integer-x points). Sign flags vanish after this. --

    // Pair (v1, vm1):  E_1 = (v1+vm1)/2 = c0+c2+c4+c6+c8+c10;  D_1 = (v1-vm1)/2.
    if (sign_vm1) {
        subtract_unsigned_spans(v1, v1_view, vm1_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(v1);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    if (sign_vm1) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm1, v1_view, vm1_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm1, v1_view, vm1_view);
    }

    // Pair (v2, vm2):  E_2 = (v2+vm2)/2;  D_2 = (v2-vm2)/4.
    if (sign_vm2) {
        subtract_unsigned_spans(v2, v2_view, vm2_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(v2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    if (sign_vm2) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm2, v2_view, vm2_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm2, v2_view, vm2_view);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(vm2);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Pair (v4, vm4):  E_4 = (v4+vm4)/2;  D_4 = (v4-vm4)/8.
    if (sign_vm4) {
        subtract_unsigned_spans(v4, v4_view, vm4_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(v4, v4_view, vm4_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(v4);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    if (sign_vm4) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vm4, v4_view, vm4_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vm4, v4_view, vm4_view);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_n(vm4, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Pair (vh, vmh):  E_h = (vh+vmh_signed)/2 = 2048c0+512c2+128c4+32c6+8c8+2c10;
    //                  D_h = (vh-vmh_signed)/2 = 1024c1+256c3+64c5+16c7+4c9+c11.
    if (sign_vmh) {
        subtract_unsigned_spans(vh, vh_view, vmh_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vh, vh_view, vmh_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(vh);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    if (sign_vmh) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vmh, vh_view, vmh_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vmh, vh_view, vmh_view);
    }

    // Pair (vq, vmq):  E_q = (vq+vmq_signed)/2 = 4194304c0+262144c2+16384c4+1024c6+64c8+4c10;
    //                  D_q = (vq-vmq_signed)/2 = 1048576c1+65536c3+4096c5+256c7+16c9+c11.
    if (sign_vmq) {
        subtract_unsigned_spans(vq, vq_view, vmq_view);
    } else {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vq, vq_view, vmq_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    }
    {
        [[maybe_unused]] const auto rem = shift_right_one(vq);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }
    if (sign_vmq) {
        [[maybe_unused]] const bool carry_out = add_unsigned_spans(vmq, vq_view, vmq_view);
        BEMAN_BIG_INT_DEBUG_ASSERT(!carry_out);
    } else {
        subtract_unsigned_spans(vmq, vq_view, vmq_view);
    }

    // -- Phase 2: subtract c0 contributions from {v1,v2,v4,vh,vq}; subtract c11
    // contributions from {vm1,vm2,vm4,vmh,vmq}; normalize vh,vq,vmh,vmq so all
    // five rows of each subsystem share the same 5x5 matrix M:
    //
    //   M = [   1      1      1       1         1     ;
    //          4     16     64     256      1024    ;
    //         16    256   4096   65536   1048576    ;
    //        256     64     16       4         1    ;
    //      65536   4096    256      16         1    ]
    //
    // (rows = v1, v2, v4, vh-normalized, vq-normalized; cols = c2 c4 c6 c8 c10
    // or c1 c3 c5 c7 c9 in the odd system). --

    // Even side: subtract c0 and powers-of-c0.
    subtract_unsigned_spans(v1, v1_view, v0_view);
    subtract_unsigned_spans(v2, v2_view, v0_view);
    subtract_unsigned_spans(v4, v4_view, v0_view);

    // vh -= 2048*c0; vh /= 2.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    std::size_t td_size = trimmed_size_span(v0_view);
    td_size             = shift_left_n(tmp_double, td_size, 11u);
    subtract_unsigned_spans(vh, vh_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    {
        [[maybe_unused]] const auto rem = shift_right_one(vh);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // vq -= 4194304*c0; vq /= 4.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(v0_view, tmp_double.begin());
    td_size = trimmed_size_span(v0_view);
    td_size = shift_left_n(tmp_double, td_size, 22u);
    subtract_unsigned_spans(vq, vq_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});
    {
        [[maybe_unused]] const auto rem = shift_right_n(vq, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // Odd side: subtract c11 and powers-of-c11. (c11 may be zero when b6 is empty;
    // the subtractions and shifts are then no-ops.)
    subtract_unsigned_spans(vm1, vm1_view, vinf_view);

    // vm2 -= 1024*c11.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vinf_view, tmp_double.begin());
    td_size = vinf_size != 0 ? trimmed_size_span(vinf_view) : std::size_t{0};
    td_size = shift_left_n(tmp_double, td_size, 10u);
    subtract_unsigned_spans(vm2, vm2_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // vm4 -= 1048576*c11.
    std::ranges::fill(tmp_double, uint_multiprecision_t{0});
    std::ranges::copy(vinf_view, tmp_double.begin());
    td_size = vinf_size != 0 ? trimmed_size_span(vinf_view) : std::size_t{0};
    td_size = shift_left_n(tmp_double, td_size, 20u);
    subtract_unsigned_spans(vm4, vm4_view, std::span<const uint_multiprecision_t>{tmp_double.data(), td_size});

    // vmh -= c11; vmh /= 4.
    subtract_unsigned_spans(vmh, vmh_view, vinf_view);
    {
        [[maybe_unused]] const auto rem = shift_right_n(vmh, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // vmq -= c11; vmq /= 16.
    subtract_unsigned_spans(vmq, vmq_view, vinf_view);
    {
        [[maybe_unused]] const auto rem = shift_right_n(vmq, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
    }

    // The odd rows D_2' and D_4' have coefficients [1, 4, ...] and [1, 16, ...]
    // respectively, one factor of x^2 less than the corresponding E_2' and E_4'
    // rows ([4, 16, ...], [16, 256, ...]). To reuse the same 5x5 elimination
    // chain for the odd subsystem, we pre-scale vm2 by 4 and vm4 by 16 so the
    // odd matrix matches the even matrix shape. The recovered c1, c3, c5, c7,
    // c9 are unaffected by this scaling (they are inputs to the matrix, not
    // outputs of it; only the right-hand side scales accordingly).
    {
        // vm2 *= 4.
        std::size_t s = trimmed_size_span(vm2_view);
        s             = shift_left_n(vm2, s, 2u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= vm2.size());
    }
    {
        // vm4 *= 16.
        std::size_t s = trimmed_size_span(vm4_view);
        s             = shift_left_n(vm4, s, 4u);
        BEMAN_BIG_INT_DEBUG_ASSERT(s <= vm4.size());
    }

    // -- Phase 3: solve the two 5x5 systems via the same elimination chain.
    // The two diagonals fold into sum/diff palindromic combinations:
    //   S_2 = row(v_2) + 4*row(v_h_n) = 1028 s_o + 272 s_i + 128 m
    //   S_4 = row(v_4) + 16*row(v_q_n) = 1048592 s_o + 65792 s_i + 8192 m
    //   D_2 = row(v_2) - 4*row(v_h_n) = -1020 d_o - 240 d_i  (signed)
    //   D_4 = row(v_4) - 16*row(v_q_n) = -1048560 d_o - 65280 d_i  (signed)
    // where s_o = c_outer_sum, s_i = c_inner_sum, m = middle coefficient (c6 or c5),
    // and d_o = c_outer_diff, d_i = c_inner_diff. After elimination:
    //   middle:   v1 holds m.
    //   sums:     vq holds s_o (= c2+c10 or c1+c9), vh holds s_i (= c4+c8 or c3+c7).
    //   diffs:    v4 holds |d_o| with sign_d_outer, v2 holds |d_i| with sign_d_inner.
    // Finally recover c2,c10 (or c1,c9) and c4,c8 (or c3,c7) using sign-aware
    // half-sum-and-difference in v2/vh and v4/vq respectively. --

    // Lambda capturing the elimination for one side (works on a, b, c, d, e where
    // a=row-of-ones buffer, b/c=row-2/row-4 buffers, d=row-h buffer, e=row-q buffer).
    // After return, a holds the middle coeff, b holds |d_inner| with returned sign_d_inner,
    // c holds |d_outer| with returned sign_d_outer, d holds s_inner, e holds s_outer.
    const auto solve_subsystem =
        [&](std::span<uint_multiprecision_t> a_buf,
            std::span<uint_multiprecision_t> b_buf,
            std::span<uint_multiprecision_t> c_buf,
            std::span<uint_multiprecision_t> d_buf,
            std::span<uint_multiprecision_t> e_buf) constexpr noexcept -> std::pair<bool, bool> {
        const auto a_v = std::span<const uint_multiprecision_t>{a_buf};
        const auto b_v = std::span<const uint_multiprecision_t>{b_buf};
        const auto c_v = std::span<const uint_multiprecision_t>{c_buf};
        const auto d_v = std::span<const uint_multiprecision_t>{d_buf};
        const auto e_v = std::span<const uint_multiprecision_t>{e_buf};

        // Compute 4*d in tmp_double. d here is 5x palindromic row, value fits in 2k+~10 bits.
        std::ranges::fill(tmp_double, uint_multiprecision_t{0});
        std::ranges::copy(d_buf, tmp_double.begin());
        std::size_t s = trimmed_size_span(d_v);
        s             = shift_left_n(tmp_double, s, 2u);

        // d_buf <- b + 4*d = S_2.
        [[maybe_unused]] const bool s2_carry =
            add_unsigned_spans(d_buf, b_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        BEMAN_BIG_INT_DEBUG_ASSERT(!s2_carry);
        // b_buf <- |b - 4*d| with sign.
        const auto sub_d2 =
            subtract_unsigned_spans_signed(b_buf, b_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        const bool sign_D2 = sub_d2.negative;

        // Compute 16*e in tmp_double.
        std::ranges::fill(tmp_double, uint_multiprecision_t{0});
        std::ranges::copy(e_buf, tmp_double.begin());
        s = trimmed_size_span(e_v);
        s = shift_left_n(tmp_double, s, 4u);

        // e_buf <- c + 16*e = S_4.
        [[maybe_unused]] const bool s4_carry =
            add_unsigned_spans(e_buf, c_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        BEMAN_BIG_INT_DEBUG_ASSERT(!s4_carry);
        // c_buf <- |c - 16*e| with sign.
        const auto sub_d4 =
            subtract_unsigned_spans_signed(c_buf, c_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});
        const bool sign_D4 = sub_d4.negative;

        // Eliminate the middle coefficient m from S_2 and S_4 using a (the all-ones row).
        // d_buf -= 128*a.
        std::ranges::fill(tmp_double, uint_multiprecision_t{0});
        std::ranges::copy(a_buf, tmp_double.begin());
        s = trimmed_size_span(a_v);
        s = shift_left_n(tmp_double, s, 7u);
        subtract_unsigned_spans(d_buf, d_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});

        // e_buf -= 8192*a.
        std::ranges::fill(tmp_double, uint_multiprecision_t{0});
        std::ranges::copy(a_buf, tmp_double.begin());
        s = trimmed_size_span(a_v);
        s = shift_left_n(tmp_double, s, 13u);
        subtract_unsigned_spans(e_buf, e_v, std::span<const uint_multiprecision_t>{tmp_double.data(), s});

        // After elimination:
        //   d_buf = 900*s_o + 144*s_i
        //   e_buf = 1040400*s_o + 57600*s_i

        // Eliminate s_inner from e_buf: e_buf -= 400 * d_buf.
        // (Trim to satisfy multiply_single_limb's `result.size() >= a.size() + 1` precondition.)
        {
            const auto                  d_trim = d_v.first(trimmed_size_span(d_v));
            [[maybe_unused]] const auto m400   = multiply_single_limb(tmp_double, d_trim, uint_multiprecision_t{400});
            subtract_unsigned_spans(e_buf, e_v, std::span<const uint_multiprecision_t>{tmp_double.data(), m400});
        }
        // e_buf = 680400 * s_o.
        {
            [[maybe_unused]] const auto rem = divide_unsigned_short(e_buf, e_v, uint_multiprecision_t{680400});
            BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
        }
        // e_buf = s_o now.

        // Recover s_inner: d_buf = 900*s_o + 144*s_i -> d_buf -= 900*s_o; d_buf /= 144.
        {
            const auto                  e_trim = e_v.first(trimmed_size_span(e_v));
            [[maybe_unused]] const auto m900   = multiply_single_limb(tmp_double, e_trim, uint_multiprecision_t{900});
            subtract_unsigned_spans(d_buf, d_v, std::span<const uint_multiprecision_t>{tmp_double.data(), m900});
        }
        {
            [[maybe_unused]] const auto rem = divide_unsigned_short(d_buf, d_v, uint_multiprecision_t{144});
            BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
        }
        // d_buf = s_i.

        // Recover middle: a_buf -= s_o; a_buf -= s_i.
        subtract_unsigned_spans(a_buf, a_v, e_v);
        subtract_unsigned_spans(a_buf, a_v, d_v);
        // a_buf = m.

        // Now solve the 2x2 for d_outer/d_inner from D_2 (b_buf, sign_D2) and D_4 (c_buf, sign_D4):
        //   -D_2 = 1020 d_o + 240 d_i
        //   -D_4 = 1048560 d_o + 65280 d_i
        // Goal: c_buf <- |d_outer| with sign_d_outer; b_buf <- |d_inner| with sign_d_inner.
        //
        // Eliminate d_inner: compute X = -D_4 - 272*(-D_2) = -D_4 + 272*D_2 = 771120 * d_outer.
        // In signed form: X_signed = 272 * b_signed - c_signed, where b_signed = D_2_signed = (sign_D2 ? -|b| : +|b|)
        // and c_signed = D_4_signed similarly.
        //
        // Implementation: compute 272*|b| into tmp_double. Then combine with |c| based on sign relations.

        const auto                  b_diff_trim = b_v.first(trimmed_size_span(b_v));
        [[maybe_unused]] const auto m272  = multiply_single_limb(tmp_double, b_diff_trim, uint_multiprecision_t{272});
        const auto                  td272 = std::span<const uint_multiprecision_t>{tmp_double.data(), m272};
        // X_signed = (sign_D2 ? -272|b| : +272|b|) - (sign_D4 ? -|c| : +|c|)
        //          = sign_D2 ? (-272|b| - (sign_D4 ? -|c| : +|c|)) : (272|b| - (sign_D4 ? -|c| : +|c|))
        // Cases:
        //   sign_D2=false, sign_D4=false: X = 272|b| - |c|.  Use subtract_signed.
        //   sign_D2=false, sign_D4=true:  X = 272|b| + |c|.  Add (positive).
        //   sign_D2=true,  sign_D4=false: X = -272|b| - |c| = -(272|b|+|c|).  Add, sign true.
        //   sign_D2=true,  sign_D4=true:  X = -272|b| + |c| = |c| - 272|b|.  Use subtract_signed (swap).
        bool sign_X = false;
        if (sign_D2 == sign_D4) {
            // Same sign: result magnitude = |272|b| - |c||; sign = sign_D2 XOR (sub_negative).
            const auto sx = subtract_unsigned_spans_signed(c_buf, td272, c_v);
            sign_X        = sign_D2 ^ sx.negative;
        } else {
            // Different signs: result magnitude = 272|b| + |c|; sign = sign_D2.
            [[maybe_unused]] const bool carry = add_unsigned_spans(c_buf, td272, c_v);
            BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
            sign_X = sign_D2;
        }
        // c_buf now holds |X|; X_signed = 771120 * d_outer; therefore
        // sign of d_outer = sign_X (771120 > 0); magnitude |d_outer| = |X|/771120.
        {
            [[maybe_unused]] const auto rem = divide_unsigned_short(
                c_buf, std::span<const uint_multiprecision_t>{c_buf}, uint_multiprecision_t{771120});
            BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
        }
        const bool sign_d_outer = sign_X;
        // c_buf = |d_outer|.

        // Recover d_inner from -D_2 = 1020*d_outer + 240*d_inner, i.e.,
        //   240 * d_inner_signed = (-D_2) - 1020 * d_outer
        //                        = -(sign_D2 ? -|b| : +|b|) - 1020 * (sign_d_outer ? -|c| : +|c|)
        // Compute 1020 * |c| into tmp_double, sign = sign_d_outer.
        const auto                  c_buf_view = std::span<const uint_multiprecision_t>{c_buf};
        const auto                  c_trim     = c_buf_view.first(trimmed_size_span(c_buf_view));
        [[maybe_unused]] const auto m1020      = multiply_single_limb(tmp_double, c_trim, uint_multiprecision_t{1020});
        const auto                  td1020     = std::span<const uint_multiprecision_t>{tmp_double.data(), m1020};
        // -D_2 has sign !sign_D2. So we compute (-D_2) + (-(1020*d_outer)) signed-result.
        // = (-D_2) - 1020*d_outer signed.
        // sign of -D_2 = !sign_D2 (we ADD its magnitude to nothing yet; first put it).
        // Let's compute Y = (-D_2) - 1020 * d_outer in signed form.
        //   first_term: |b| with sign !sign_D2 (since first_term = -D_2 = -(sign_D2 ? -|b| : +|b|))
        //   second_term: 1020 * (sign_d_outer ? -|c| : +|c|) = sign_d_outer ? -1020|c| : +1020|c|
        //   Y = first_term - second_term
        const bool sign_first  = !sign_D2;
        const bool sign_second = sign_d_outer;
        bool       sign_Y      = false;
        if (sign_first == sign_second) {
            const auto sy = subtract_unsigned_spans_signed(b_buf, b_v, td1020);
            sign_Y        = sign_first ^ sy.negative;
        } else {
            [[maybe_unused]] const bool carry = add_unsigned_spans(b_buf, b_v, td1020);
            BEMAN_BIG_INT_DEBUG_ASSERT(!carry);
            sign_Y = sign_first;
        }
        // Y = 240 * d_inner. Therefore |d_inner| = |Y|/240, sign_d_inner = sign_Y.
        {
            [[maybe_unused]] const auto rem = divide_unsigned_short(
                b_buf, std::span<const uint_multiprecision_t>{b_buf}, uint_multiprecision_t{240});
            BEMAN_BIG_INT_DEBUG_ASSERT(rem == 0);
        }
        const bool sign_d_inner = sign_Y;

        return {sign_d_outer, sign_d_inner};
    };

    // Solve even subsystem in {v1, v2, v4, vh, vq}.
    const auto [sign_d_outer_e, sign_d_inner_e] = solve_subsystem(v1, v2, v4, vh, vq);
    // Now:  v1 = c6;  vh = c4+c8;  vq = c2+c10;  v4 = |c2 - c10|, sign_d_outer_e;  v2 = |c4 - c8|, sign_d_inner_e.

    // Solve odd subsystem in {vm1, vm2, vm4, vmh, vmq}. The odd buffers were
    // pre-scaled in Phase 2 (vm2*=4, vm4*=16) so the same elimination chain
    // applies.
    const auto [sign_d_outer_o, sign_d_inner_o] = solve_subsystem(vm1, vm2, vm4, vmh, vmq);
    // Now:  vm1 = c5;  vmh = c3+c7;  vmq = c1+c9;  vm4 = |c1 - c9|, sign_d_outer_o;  vm2 = |c3 - c7|, sign_d_inner_o.

    // -- Recover individual c-values from sum/diff pairs --
    //
    //   c_outer  = (s_outer + d_outer) / 2
    //   c_outer' = (s_outer - d_outer) / 2
    // (with sign-aware diff). The lower-index coefficient (c2, c4 / c1, c3) gets
    // s+d when d_outer is positive (c_lower > c_higher), otherwise s-d.
    //
    // We're going to place:
    //   c2 in v4  (overwriting |d_outer|), c10 in vq (overwriting s_outer)
    //   c4 in v2  (overwriting |d_inner|), c8 in vh (overwriting s_inner)
    //   c1 in vm4 (overwriting |d_outer|), c9 in vmq
    //   c3 in vm2 (overwriting |d_inner|), c7 in vmh
    //
    // For each pair (s, d) -> (lower, higher):
    //   if sign_d:   lower = (s - |d|)/2;  higher = (s + |d|)/2
    //   else:        lower = (s + |d|)/2;  higher = (s - |d|)/2

    // Even outer pair: s_outer in vq, d_outer in v4 (sign_d_outer_e), lower = c2, higher = c10.
    // We need to place c2 somewhere distinct from s_outer/d_outer to avoid aliasing.
    // Use v4 for c2 and vq for c10 (overwrite both, since s_outer and d_outer are no longer needed
    // independently after this step). But recover_pair reads s_view (vq) and d_view (v4), and
    // writes to lower_dst and higher_dst. If lower_dst == v4 and higher_dst == vq, then writes
    // overlap with reads. To avoid aliasing, recover_pair uses tmp_double for the plus computation.
    // The minus computation overwrites the destination, and it reads s_view (separate). So aliasing
    // s_view with one of the destinations is OK as long as the minus computation finishes before
    // any subsequent read of s_view. Since recover_pair does minus first then copies plus, OK.
    recover_pair(v4,
                 vq,
                 std::span<const uint_multiprecision_t>{vq}, // s_outer
                 std::span<const uint_multiprecision_t>{v4}, // |d_outer|
                 sign_d_outer_e,
                 tmp_double);
    // After: v4 = c2, vq = c10.

    // Even inner pair: s_inner in vh, d_inner in v2 (sign_d_inner_e), lower = c4, higher = c8.
    recover_pair(v2,
                 vh,
                 std::span<const uint_multiprecision_t>{vh},
                 std::span<const uint_multiprecision_t>{v2},
                 sign_d_inner_e,
                 tmp_double);
    // After: v2 = c4, vh = c8.

    // Odd outer pair: s_outer in vmq, d_outer in vm4 (sign_d_outer_o), lower = c1, higher = c9.
    recover_pair(vm4,
                 vmq,
                 std::span<const uint_multiprecision_t>{vmq},
                 std::span<const uint_multiprecision_t>{vm4},
                 sign_d_outer_o,
                 tmp_double);
    // After: vm4 = c1, vmq = c9.

    // Odd inner pair: s_inner in vmh, d_inner in vm2 (sign_d_inner_o), lower = c3, higher = c7.
    recover_pair(vm2,
                 vmh,
                 std::span<const uint_multiprecision_t>{vmh},
                 std::span<const uint_multiprecision_t>{vm2},
                 sign_d_inner_o,
                 tmp_double);
    // After: vm2 = c3, vmh = c7.

    // -- Recompose: place each c_i at offset i*k in result. c0 (in result[0..2k))
    // and c11 (in result[11k..)) are already in position.
    //   c1  in vm4 -> offset  1*k
    //   c2  in v4  -> offset  2*k
    //   c3  in vm2 -> offset  3*k
    //   c4  in v2  -> offset  4*k
    //   c5  in vm1 -> offset  5*k
    //   c6  in v1  -> offset  6*k
    //   c7  in vmh -> offset  7*k
    //   c8  in vh  -> offset  8*k
    //   c9  in vmq -> offset  9*k
    //   c10 in vq  -> offset 10*k
    add_shifted(result, 1 * k, vm4_view);
    add_shifted(result, 2 * k, v4_view);
    add_shifted(result, 3 * k, vm2_view);
    add_shifted(result, 4 * k, v2_view);
    add_shifted(result, 5 * k, vm1_view);
    add_shifted(result, 6 * k, v1_view);
    add_shifted(result, 7 * k, vmh_view);
    add_shifted(result, 8 * k, vh_view);
    add_shifted(result, 9 * k, vmq_view);
    add_shifted(result, 10 * k, vq_view);

    // Release scratch back to the bump pool for sibling reuse.
    scratch.deallocate(total_scratch);
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

    // Choose algorithm to use based off the tuned cutoffs
    // Avoid these at compile time because the recursion depth could blow up consteval limits;
    // long multiplication works just fine in that case.
    if BEMAN_BIG_INT_IS_NOT_CONSTEVAL {
        if (a.size() >= toom_cook_6_5_cutoff && b.size() >= toom_cook_6_5_cutoff) {
            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t storage_size = toom_cook_6_5_storage_size(s);
            const std::size_t result_total = a.size() + b.size();

            scratch_allocator<Allocator> scratch(storage_size, alloc);
            multiply_toom_cook_6_5(result.first(result_total), a, b, scratch);
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
        if (a.size() >= toom_cook_4_cutoff && b.size() >= toom_cook_4_cutoff) {
            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t storage_size = toom_cook_4_storage_size(s);
            const std::size_t result_total = a.size() + b.size();

            scratch_allocator<Allocator> scratch(storage_size, alloc);
            multiply_toom_cook_4(result.first(result_total), a, b, scratch);
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
        if (a.size() >= toom_cook_3_cutoff && b.size() >= toom_cook_3_cutoff) {
            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t storage_size = toom_cook_3_storage_size(s);
            const std::size_t result_total = a.size() + b.size();

            scratch_allocator<Allocator> scratch(storage_size, alloc);
            multiply_toom_cook_3(result.first(result_total), a, b, scratch);
            return trimmed_size_span(std::span<const uint_multiprecision_t>{result.data(), result_total});
        }
        if (a.size() >= karatsuba_cutoff && b.size() >= karatsuba_cutoff) {
            const std::size_t s            = std::max(a.size(), b.size());
            const std::size_t storage_size = karatsuba_storage_size(s);
            const std::size_t result_total = a.size() + b.size();

            if (storage_size <= karatsuba_stack_threshold) {
                uint_multiprecision_t        stack_buf[karatsuba_stack_threshold];
                scratch_allocator<Allocator> scratch(stack_buf, karatsuba_stack_threshold, alloc);
                multiply_karatsuba(result.first(result_total), a, b, scratch);
            } else {
                scratch_allocator<Allocator> scratch(storage_size, alloc);
                multiply_karatsuba(result.first(result_total), a, b, scratch);
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
